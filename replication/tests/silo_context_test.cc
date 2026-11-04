#include "../../benchmarks/bench.h"
#include "../../benchmarks/ndb_wrapper_impl.h"
#include "../../benchmarks/worker_context.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

#ifndef SILO_WORKER_CONTEXTS
#error "Build this test with WORKER_CONTEXTS=1"
#endif

// The standalone executable links the Silo adapter without the benchmark runner.
size_t nthreads = 2;
int verbose = 0;

namespace {

const std::string key = "shared";
const std::string initial = "before";
const std::string updated(128, 'u');
constexpr uint64_t snapshot = transaction_base::TXN_FLAG_READ_ONLY;

struct ScanError {};

template <typename Predicate>
void wait_until(Predicate ready)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!ready()) {
    ALWAYS_ASSERT(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template <typename Action>
void expect_blocked_transition(Action action)
{
  try {
    action();
  } catch (const std::logic_error &) {
    return;
  }
  ALWAYS_ASSERT(false);
}

struct ProtectedScan : abstract_ordered_index::scan_callback {
  WorkerContext &context;
  bool seen = false;
  bool fail;

  ProtectedScan(WorkerContext &context, bool fail)
    : context(context), fail(fail) {}

  bool invoke(const char *data, size_t size, const std::string &value) override
  {
    ALWAYS_ASSERT(std::string(data, size) == key);
    ALWAYS_ASSERT(value == initial || (fail && value == updated));
    seen = true;
    if (!fail)
      context.request_service();
    {
      ScopedNonPreemptible inner;
      ALWAYS_ASSERT(!context.checkpoint());
    }
    ALWAYS_ASSERT(ScopedNonPreemptible::blocked());
    ALWAYS_ASSERT(!context.checkpoint());
    expect_blocked_transition([&] { context.yield(); });
    if (fail)
      throw ScanError{};
    return false;
  }
};

struct RetiredValue {
  std::string value = "live across the switch";
  bool &reaped;

  explicit RetiredValue(bool &reaped) : reaped(reaped) {}
  ~RetiredValue() { reaped = true; }
};

struct Fixture {
  ndb_wrapper<transaction_proto2> db{{}, {}, false, false, false};
  abstract_ordered_index *index = db.open_index("i08", 256, false);
  bench_txn_context low{1, db, snapshot};
  bench_txn_context high{2, db, 0};
  bool reaped = false;
  RetiredValue *retired = new RetiredValue(reaped);
  uint64_t low_tick = 0;

  void *begin(bench_txn_context &state, uint64_t flags = 0)
  {
    return db.new_txn(flags, state.arena, state.txn_buf(), abstract_db::HINT_DEFAULT);
  }

  static void low_entry(WorkerContext &context, void *argument)
  {
    auto &f = *static_cast<Fixture *>(argument);
    scoped_str_arena arena(f.low.arena);
    auto &value = f.low.str();
    auto &marker = f.low.str();
    marker = "low scratch";
    const auto *live = f.retired;
    f.low.r.next();
    const auto seed = f.low.r.get_seed();

    void *txn = f.begin(f.low, snapshot);
    ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
    ALWAYS_ASSERT(ticker::s_instance.is_locally_guarded(f.low_tick));
    ALWAYS_ASSERT(f.index->get(txn, key, value) && value == initial);
    ALWAYS_ASSERT(!context.checkpoint());

    ProtectedScan scan(context, false);
    f.index->scan(txn, key, nullptr, scan, arena.get());
    ALWAYS_ASSERT(scan.seen);
    ALWAYS_ASSERT(context.checkpoint());

    uint64_t tick;
    ALWAYS_ASSERT(ticker::s_instance.is_locally_guarded(tick) && tick == f.low_tick);
    ALWAYS_ASSERT(!f.reaped && live->value == "live across the switch");
    ALWAYS_ASSERT(f.low.r.get_seed() == seed && marker == "low scratch");
    ALWAYS_ASSERT(value == initial);
    ALWAYS_ASSERT(f.index->get(txn, key, value) && value == initial);
    ALWAYS_ASSERT(f.db.commit_txn(txn));

    // The throwing callback releases its protection on unwind. The original
    // stack rearmed the request while servicing the first handoff.
    txn = f.begin(f.low, snapshot);
    ProtectedScan failing(context, true);
    try {
      f.index->rscan(txn, key, nullptr, failing, arena.get());
      ALWAYS_ASSERT(false);
    } catch (const ScanError &) {
      ALWAYS_ASSERT(failing.seen);
      ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
      f.db.abort_txn(txn);
    }
    ALWAYS_ASSERT(context.checkpoint());
    ALWAYS_ASSERT(!context.checkpoint());

    // A successful insert holds a tuple lock across adapter calls. It must
    // also block a raw yield until the attempt has been resolved.
    txn = f.begin(f.low);
    f.index->insert(txn, "low-abort", "discard");
    context.request_service();
    ALWAYS_ASSERT(!context.checkpoint());
    expect_blocked_transition([&] { context.yield(); });
    f.db.abort_txn(txn);
    ALWAYS_ASSERT(context.checkpoint());
  }

  void run()
  {
    ALWAYS_ASSERT(high.txn_buf() != low.txn_buf());
    db.thread_init(false);
    {
      scoped_str_arena arena(high.arena);
      void *txn = begin(high);
      index->insert(txn, key, initial);
      ALWAYS_ASSERT(db.commit_txn(txn));
    }
    const uint64_t seeded_tick = ticker::s_instance.global_current_tick();
    wait_until([&] {
      const auto tick = ticker::s_instance.global_last_tick_exclusive();
      const auto width = transaction_proto2_static::ReadOnlyEpochMultiplier;
      return tick / width * width > seeded_tick;
    });

    WorkerContext context(low_entry, this);
    {
      ScopedNonPreemptible guard;
      expect_blocked_transition([&] { context.resume(); });
    }
    ALWAYS_ASSERT(context.resume());
    ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
    uint64_t tick;
    ALWAYS_ASSERT(ticker::s_instance.is_locally_guarded(tick) && tick == low_tick);
    {
      scoped_str_arena arena(high.arena);
      high.r.set_seed(999);
      high.r.next();
      high.str() = "high scratch 1";
      high.str() = "high scratch 2";
      void *txn = begin(high);
      index->put(txn, key, updated);
      rcu::s_instance.free(retired);
      ALWAYS_ASSERT(db.commit_txn(txn));
    }
    wait_until([&] { return ticker::s_instance.global_current_tick() > low_tick; });
    ALWAYS_ASSERT(ticker::s_instance.global_last_tick_exclusive() <= low_tick);
    ALWAYS_ASSERT(!reaped && rcu::s_instance.in_rcu_region());
    std::puts("PASS live snapshot and isolated context state across a short commit");

    context.request_service();
    ALWAYS_ASSERT(context.resume());
    ALWAYS_ASSERT(!rcu::s_instance.in_rcu_region());
    ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
    ALWAYS_ASSERT(context.resume());
    ALWAYS_ASSERT(!rcu::s_instance.in_rcu_region());
    ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
    ALWAYS_ASSERT(!context.resume());
    std::puts("PASS nested scan protection, deferred service, callback unwind and insert abort");

    // Hold a real read set open while a separate worker changes its record.
    // A false commit must keep the object alive until the caller aborts it.
    {
      scoped_str_arena arena(high.arena);
      void *txn = begin(high);
      std::string value;
      ALWAYS_ASSERT(!index->get(txn, "low-abort", value));
      ALWAYS_ASSERT(index->get(txn, key, value) && value == updated);
      std::thread writer([&] {
        bench_txn_context other(3, db, 0);
        void *write = begin(other);
        index->put(write, key, "conflict");
        ALWAYS_ASSERT(db.commit_txn(write));
      });
      writer.join();
      ALWAYS_ASSERT(!db.commit_txn(txn));
      ALWAYS_ASSERT(ScopedNonPreemptible::blocked());
      db.abort_txn(txn);
      ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
      ALWAYS_ASSERT(!rcu::s_instance.in_rcu_region());
    }
    std::puts("PASS conflict, abort cleanup and balanced worker guards");

    wait_until([&] {
      { scoped_rcu_region poll; }
      return reaped;
    });
    std::puts("PASS deferred reclamation after the snapshot ends");
    db.thread_end();
    db.close_index(index);
  }
};

} // namespace

int main()
{
  Fixture fixture;
  fixture.run();
}
