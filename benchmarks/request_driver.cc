#include "request_driver.h"

#ifdef SILO_WORKER_CONTEXTS
#include "worker_context.h"
#include "../preemption.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>

namespace queued {
namespace {

// The producer owns tail, the worker owns head. Each release publishes either
// a filled slot or a reusable slot. One extra slot distinguishes full from empty.
class Queue {
public:
  explicit Queue(size_t capacity) : slots(capacity + 1) {}

  bool empty() const {
    return head.load(std::memory_order_acquire) ==
           tail.load(std::memory_order_acquire);
  }

  bool push(const Request &request) {
    const size_t pos = tail.load(std::memory_order_relaxed);
    const size_t next = (pos + 1) % slots.size();
    if (next == head.load(std::memory_order_acquire))
      return false;
    slots[pos] = request;
    tail.store(next, std::memory_order_release);
    return true;
  }

  bool pop(Request &request) {
    const size_t pos = head.load(std::memory_order_relaxed);
    if (pos == tail.load(std::memory_order_acquire))
      return false;
    request = slots[pos];
    head.store((pos + 1) % slots.size(), std::memory_order_release);
    return true;
  }

private:
  std::vector<Request> slots;
  std::atomic<size_t> head{0};
  std::atomic<size_t> tail{0};
};

#ifdef SILO_WORKER_CONTEXTS
struct Cooperative {
  explicit Cooperative(std::function<void(Request &)> execute)
    : execute(std::move(execute)), context(run, this) {}

  static void run(WorkerContext &context, void *argument) {
    auto &scheduler = *static_cast<Cooperative *>(argument);
    while (scheduler.low) {
      scheduler.execute(*scheduler.low);
      scheduler.low = nullptr;
      context.yield();
    }
  }

  const std::function<void(Request &)> execute;
  Request *low = nullptr;
  uint32_t reads_left = 0;
  bool pending = false;
  WorkerContext context;
};
#endif

struct Worker {
  explicit Worker(size_t capacity) : high(capacity), low(1) {}

  Queue high;
  Queue low;
  std::mutex mutex;
  std::condition_variable wake;
  std::vector<Request> records;
  NotificationStats notifications;
  std::atomic<uint64_t> deliveries{0};
  std::atomic<bool> notification_pending{false};
#ifdef SILO_WORKER_CONTEXTS
  Cooperative *cooperative = nullptr; // Owned by the worker's original stack.
#endif
};

// SplitMix64 finalizer. High input seeds use the high sequence, independently
// of low replenishment and overload. Each low stream has a worker-local ordinal.
uint64_t mix(uint64_t value) {
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

const char *name(Kind kind) {
  switch (kind) {
  case Kind::new_order: return "NewOrder";
  case Kind::payment: return "Payment";
  case Kind::synthetic_low: return "SyntheticLow";
  case Kind::q2: return "Q2";
  }
  std::terminate();
}

const char *name(Outcome outcome) {
  switch (outcome) {
  case Outcome::committed: return "committed";
  case Outcome::user_aborted: return "user_aborted";
  case Outcome::retry_exhausted: return "retry_exhausted";
  case Outcome::failed: return "failed";
  case Outcome::rejected_queue_full: return "rejected_queue_full";
  case Outcome::cancelled_shutdown: return "cancelled_shutdown";
  case Outcome::pending: break;
  }
  std::terminate();
}

void optional_ns(std::ostream &out, Ns value) {
  if (value)
    out << value;
  else
    out << "null";
}

} // namespace

Ns monotonic_now() {
  timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    std::terminate();
  return Ns(time.tv_sec) * second + time.tv_nsec;
}

struct Driver::State {
  State(const Config &config, Clock clock, Wait wait_until, HighPublished high_published,
        TransitionHook transition)
    : config(config), clock(clock), wait_until(wait_until),
      high_published(std::move(high_published)), transition(std::move(transition)),
      low_sequence(config.workers) {}

  const Config config;
  const Clock clock;
  const Wait wait_until;
  const HighPublished high_published;
  const TransitionHook transition;
  std::vector<std::unique_ptr<Worker>> workers;
  std::vector<uint64_t> low_sequence;
  std::deque<Request> global;
  std::vector<Request> producer_records;
  std::mutex ready_mutex;
  std::condition_variable ready;
  size_t ready_count = 0;
  size_t drained_count = 0;
  std::atomic<bool> closed{false};
  std::atomic<bool> fatal{false};
  std::atomic<bool> overrun{false};
  std::atomic<Ns> drain_deadline{0};
  uint64_t count = 0;
  uint64_t high_count = 0;
  Ns cutoff = 0;
};

Driver::Driver(const Config &config, Clock clock, Wait wait_until, HighPublished high_published,
               TransitionHook transition)
  : state(new State(config, clock, wait_until, std::move(high_published),
                    std::move(transition))) {
  if (!config.workers || !config.batch_size || !config.interval ||
      !config.global_capacity || !config.high_capacity ||
      config.high_capacity == SIZE_MAX || !config.attempt_limit || !config.yield_interval)
    throw std::invalid_argument("invalid queued request driver configuration");
  if (config.policy != Policy::wait) {
#ifdef SILO_WORKER_CONTEXTS
    if (!config.generate_low || config.low_kind != Kind::q2)
      throw std::invalid_argument("checkpoint scheduling requires a mixed Q2 workload");
#else
    throw std::invalid_argument("checkpoint scheduling requires a WORKER_CONTEXTS=1 build");
#endif
  }
  for (size_t i = 0; i < config.workers; ++i)
    state->workers.emplace_back(new Worker(config.high_capacity));
}

Driver::~Driver() = default;

void Driver::deliver_notification(size_t worker) {
  Worker &w = *state->workers.at(worker);
  w.deliveries.fetch_add(1, std::memory_order_relaxed);
  w.notification_pending.store(true, std::memory_order_release);
}

NotificationStats Driver::notification_stats(size_t worker) const {
  const Worker &w = *state->workers.at(worker);
  NotificationStats result = w.notifications;
  result.deliveries = w.deliveries.load(std::memory_order_relaxed);
  result.pending_hint = w.notification_pending.load(std::memory_order_relaxed);
  return result;
}

void Driver::notify(size_t worker) {
  // Pair the wake with the waiter's predicate check. The queue itself uses
  // atomics, but without this mutex a notification could be lost before sleep.
  Worker &w = *state->workers[worker];
  std::lock_guard<std::mutex> lock(w.mutex);
  w.wake.notify_one();
}

void Driver::produce(Ns duration, Ns drain, uint64_t first_id, uint64_t first_high) {
  State &s = *state;
  const Config &config = s.config;
  {
    std::unique_lock<std::mutex> lock(s.ready_mutex);
    s.ready.wait(lock, [&] { return s.ready_count == config.workers; });
  }
  const Ns start = s.clock();
  s.cutoff = start + duration;
  const Ns deadline = s.cutoff + drain;
  s.drain_deadline.store(deadline, std::memory_order_release);

  const auto make_request = [&](size_t worker, uint64_t seed, Kind kind,
                                uint64_t batch, Ns scheduled, Ns generated) {
    Request request;
    request.id = first_id + s.count++;
    request.batch = batch;
    request.kind = kind;
    request.worker = worker;
    request.home_warehouse = worker + 1;
    request.input_seed = seed;
    request.scheduled = scheduled;
    request.generated = generated;
    return request;
  };

  uint64_t batch = 1;
  Ns next = start + std::min(config.interval, duration);
  std::vector<bool> published(config.workers);
  for (;;) {
    const Ns now = s.clock();
    if (s.fatal.load(std::memory_order_acquire))
      break;
    // Snapshot the due set so a slow catch-up pass still gives admission a turn.
    for (; next < s.cutoff && next <= now; ++batch) {
      for (size_t i = 0; i < config.batch_size; ++i) {
        const uint64_t high_sequence = s.high_count++;
        const uint64_t seed = mix(config.seed + high_sequence);
        const Kind kind = seed >> 63 ? Kind::new_order : Kind::payment;
        Request request = make_request(high_sequence % config.workers, seed,
                                       kind, batch, next, s.clock());
        request.high_sequence = first_high + high_sequence;
        if (s.global.size() == config.global_capacity) {
          request.outcome = Outcome::rejected_queue_full;
          request.finished = request.generated;
          s.producer_records.push_back(request);
        } else {
          s.global.push_back(request);
        }
      }
      next += std::min(config.interval, s.cutoff - next);
    }

    // Account for every due batch even if the generator woke past cutoff.
    // After the drain deadline, no new work is admitted.
    if (s.clock() >= deadline)
      break;
    std::fill(published.begin(), published.end(), false);
    while (!s.global.empty()) {
      const Ns admitted = s.clock();
      if (admitted >= deadline)
        break;
      Request &request = s.global.front();
      request.admitted = admitted;
      if (!s.workers[request.worker]->high.push(request)) {
        request.admitted = 0;
        break;
      }
      published[request.worker] = true;
      ++s.workers[request.worker]->notifications.admitted_high;
      s.global.pop_front();
    }
    for (size_t i = 0; i < config.workers; ++i)
      if (published[i])
        ++s.workers[i]->notifications.publication_passes;
    if (s.high_published) {
      try {
        for (size_t i = 0; i < config.workers; ++i)
          if (published[i])
            s.high_published(i);
      } catch (...) {
        s.fatal.store(true, std::memory_order_release);
        break;
      }
    }
    if (config.generate_low) {
      for (size_t i = 0; i < config.workers && s.clock() < s.cutoff; ++i) {
        if (!s.workers[i]->low.empty())
          continue;
        const Ns generated = s.clock();
        if (generated >= s.cutoff)
          break;
        const uint64_t seed = mix(~config.seed ^ mix(i) ^ mix(++s.low_sequence[i]));
        Request request = make_request(i, seed, config.low_kind, 0, generated, generated);
        request.admitted = generated;
        // Only this producer can fill the slot after observing an empty queue.
        if (!s.workers[i]->low.push(request))
          std::terminate();
        published[i] = true;
      }
    }
    for (size_t i = 0; i < config.workers; ++i)
      if (published[i])
        notify(i);

    const Ns after = s.clock();
    if (after >= s.cutoff && next == s.cutoff && s.global.empty())
      break;
    // Poll capacity without parking behind any one worker. A timer wake never
    // moves the next scheduled deadline.
    Ns wake_at = std::min(deadline, after + 100000);
    if (next < s.cutoff || after < s.cutoff)
      wake_at = std::min(wake_at, next);
    if (wake_at > after) {
      if (s.wait_until)
        s.wait_until(wake_at);
      else
        std::this_thread::sleep_for(std::chrono::nanoseconds(wake_at - after));
    }
  }

  for (Request &request : s.global) {
    request.finished = s.clock();
    request.outcome = Outcome::cancelled_shutdown;
    s.producer_records.push_back(request);
  }
  s.global.clear();
  s.closed.store(true, std::memory_order_release);
  for (size_t i = 0; i < config.workers; ++i)
    notify(i);
}

void Driver::run_worker(size_t worker, const Execute &execute) {
  run_prepared_worker(worker, [&](const Request &request) {
    return [&](uint64_t &) { return execute(request); };
  });
}

void Driver::on_record_read(size_t worker) {
#ifdef SILO_WORKER_CONTEXTS
  State &s = *state;
  Worker &w = *s.workers[worker];
  Cooperative &scheduler = *w.cooperative;
  Request &request = *scheduler.low;
  if (s.config.policy == Policy::notification_checkpoint) {
    // Clear the hint BEFORE checking the authoritative queue. A delayed or
    // coalesced hint cannot hide work published at high-empty/low-resume.
    const bool hinted = w.notification_pending.exchange(false, std::memory_order_acq_rel);
    if (hinted)
      ++w.notifications.hints_consumed;
    if (w.high.empty()) {
      if (hinted)
        ++w.notifications.spurious_checks;
      return;
    }
    if (!hinted)
      ++w.notifications.queue_fallbacks;
    if (ScopedNonPreemptible::blocked()) {
      ++w.notifications.deferrals;
      return;
    }
    if (!s.fatal.load(std::memory_order_acquire) &&
        s.clock() < s.drain_deadline.load(std::memory_order_acquire)) {
      ++w.notifications.switches;
      scheduler.context.yield();
    }
    return;
  }
  ++request.cooperative_reads;
  if (--scheduler.reads_left == 0) {
    scheduler.reads_left = s.config.yield_interval;
    ++request.cooperative_checks;
    scheduler.pending = !w.high.empty();
  }
  if (scheduler.pending && !ScopedNonPreemptible::blocked()) {
    scheduler.pending = false;
    if (!s.fatal.load(std::memory_order_acquire) &&
        s.clock() < s.drain_deadline.load(std::memory_order_acquire)) {
      ++request.cooperative_yields;
      ++w.notifications.switches;
      scheduler.context.yield();
    }
  }
#else
  (void)worker;
  std::terminate();
#endif
}

void Driver::run_prepared_worker(size_t worker, const Prepare &prepare) {
  State &s = *state;
  Worker &w = *s.workers[worker];
  const auto execute_request = [&](Request &request) {
    const Ns deadline = s.drain_deadline.load(std::memory_order_acquire);
    Prepared execute;
    // Run the request on this worker through completion, including retries.
    while (request.outcome == Outcome::pending) {
      const Ns now = s.clock();
      if (now >= deadline || s.fatal.load(std::memory_order_acquire)) {
        request.outcome = Outcome::cancelled_shutdown;
        request.finished = now;
        break;
      }
      if (!request.attempts)
        request.first_start = now;
      ++request.attempts;
#ifdef SILO_WORKER_CONTEXTS
      if (!request.batch && w.cooperative) {
        w.cooperative->reads_left = s.config.yield_interval;
        w.cooperative->pending = false;
      }
#endif
      Attempt attempt = Attempt::failed;
      try {
        if (!execute)
          execute = prepare(request);
        attempt = execute(request.q2_checksum);
      } catch (...) {
        // The adapter must unwind any live transaction before propagating.
      }
      request.finished = s.clock();
      if (request.finished > deadline)
        s.overrun.store(true, std::memory_order_relaxed);
      switch (attempt) {
      case Attempt::committed:
        request.outcome = Outcome::committed;
        break;
      case Attempt::conflict:
        if (++request.aborts == s.config.attempt_limit)
          request.outcome = Outcome::retry_exhausted;
        else
          request.finished = 0;
        break;
      case Attempt::user_aborted:
        ++request.aborts;
        request.outcome = Outcome::user_aborted;
        break;
      case Attempt::failed:
        request.outcome = Outcome::failed;
        s.fatal.store(true, std::memory_order_release);
        break;
      }
    }
    w.records.push_back(request);
  };
#ifdef SILO_WORKER_CONTEXTS
  std::unique_ptr<Cooperative> cooperative;
  if (s.config.policy != Policy::wait) {
    try {
      cooperative.reset(new Cooperative(execute_request));
      w.cooperative = cooperative.get();
    } catch (const std::exception &) {
      s.fatal.store(true, std::memory_order_release);
    }
  }
#endif
  {
    std::lock_guard<std::mutex> lock(s.ready_mutex);
    ++s.ready_count;
    s.ready.notify_one();
  }
  for (;;) {
    Request request;
    {
      std::unique_lock<std::mutex> lock(w.mutex);
      w.wake.wait(lock, [&] {
        return s.closed.load(std::memory_order_acquire) || !w.high.empty() || !w.low.empty();
      });
      const bool closed = s.closed.load(std::memory_order_acquire);
      // Only consider low work already present when we check high, so a
      // concurrent low publication cannot slip ahead of earlier high work.
      const bool low_pending = !w.low.empty();
      if (!w.high.pop(request) && !(low_pending && w.low.pop(request))) {
        if (closed)
          break;
        continue;
      }
    }

#ifdef SILO_WORKER_CONTEXTS
    if (cooperative && !request.batch) {
      cooperative->low = &request;
      do {
        cooperative->context.resume();
        if (cooperative->low) {
          // The Q2 is paused at an audited read. Only consume high work
          // before returning to its live transaction on the alternate stack.
          Request high;
          ++w.notifications.high_entries;
          if (s.transition)
            s.transition(worker, ServiceTransition::high_entry);
          while (!s.fatal.load(std::memory_order_acquire) &&
                 s.clock() < s.drain_deadline.load(std::memory_order_acquire)) {
            if (!w.high.pop(high)) {
              ++w.notifications.empty_checks;
              if (s.transition)
                s.transition(worker, ServiceTransition::high_empty);
              break;
            }
            execute_request(high);
          }
          if (s.transition)
            s.transition(worker, ServiceTransition::before_low_resume);
          ++w.notifications.low_resumes;
        }
      } while (cooperative->low);
      continue;
    }
#endif
    execute_request(request);
  }
#ifdef SILO_WORKER_CONTEXTS
  if (cooperative) {
    // Finish the parked entry before releasing its stack. A worker with no
    // low requests can enter with a null request and return immediately.
    cooperative->context.resume();
    w.cooperative = nullptr;
  }
#endif
  std::lock_guard<std::mutex> lock(s.ready_mutex);
  ++s.drained_count;
  s.ready.notify_one();
}

void Driver::wait_for_drain() {
  State &s = *state;
  std::unique_lock<std::mutex> lock(s.ready_mutex);
  s.ready.wait(lock, [&] { return s.drained_count == s.config.workers; });
}

void Driver::visit_records(const Visit &visit) const {
  for (const Request &request : state->producer_records)
    visit(request);
  for (const auto &worker : state->workers)
    for (const Request &request : worker->records)
      visit(request);
}

uint64_t Driver::generated() const { return state->count; }
uint64_t Driver::high_generated() const { return state->high_count; }
Ns Driver::cutoff() const { return state->cutoff; }
bool Driver::failed() const { return state->fatal || state->overrun; }

void write_header(std::ostream &out) {
  out << "run_id\tphase\trequest_id\tbatch_id\ttype\tpriority\tworker\thome_warehouse"
      "\tinput_seed\tscheduled_ns\tgenerated_ns\tadmitted_ns\tfirst_start_ns"
      "\tfinished_ns\tattempts\taborts\toutcome\tgeneration_delay_ns\tq2_checksum"
      "\tcooperative_reads\tcooperative_checks\tcooperative_yields\n";
}

void write_results(const Driver &driver, std::ostream &out, std::ostream &summary,
                   const std::string &run_id, const char *phase) {
  uint64_t completed = 0, rejected = 0, cancelled = 0;
  uint64_t pending_global = 0, queued = 0, running = 0, due_ungenerated = 0;
  uint64_t commits_in_window = 0, drain_commits = 0;
  driver.visit_records([&](const Request &r) {
    out << run_id << '\t' << phase << '\t' << r.id << '\t';
    if (r.batch)
      out << r.batch;
    else
      out << "null";
    out << '\t' << name(r.kind) << '\t' << (r.batch ? "high" : "low")
        << '\t' << r.worker << '\t' << r.home_warehouse << '\t' << r.input_seed
        << '\t' << r.scheduled << '\t' << r.generated << '\t';
    optional_ns(out, r.admitted);
    out << '\t';
    optional_ns(out, r.first_start);
    out << '\t' << r.finished << '\t' << r.attempts << '\t' << r.aborts
        << '\t' << name(r.outcome) << '\t' << (r.generated - r.scheduled) << '\t';
    if (r.kind == Kind::q2 && r.outcome == Outcome::committed)
      out << r.q2_checksum;
    else
      out << "null";
    out << '\t' << r.cooperative_reads << '\t' << r.cooperative_checks
        << '\t' << r.cooperative_yields << '\n';

    if (r.outcome == Outcome::rejected_queue_full)
      ++rejected;
    else if (r.outcome == Outcome::cancelled_shutdown)
      ++cancelled;
    else
      ++completed;
    const Ns cutoff = driver.cutoff();
    if (r.outcome == Outcome::committed)
      r.finished < cutoff ? ++commits_in_window : ++drain_commits;
    // Reconstruct the cutoff state from lifecycle timestamps after joining.
    if (r.generated >= cutoff)
      ++due_ungenerated;
    else if (r.finished >= cutoff) {
      if (!r.admitted || r.admitted >= cutoff)
        ++pending_global;
      else if (!r.first_start || r.first_start >= cutoff)
        ++queued;
      else
        ++running;
    }
  });
  summary << "phase=" << phase << " cutoff_ns=" << driver.cutoff()
          << " generated=" << driver.generated() << " completed=" << completed
          << " rejected=" << rejected << " cancelled=" << cancelled << " dropped=0"
          << " cutoff_pending_global=" << pending_global << " cutoff_queued=" << queued
          << " cutoff_running=" << running << " cutoff_due_ungenerated=" << due_ungenerated
          << " commits_in_window=" << commits_in_window << " drain_commits=" << drain_commits
          << " failed=" << driver.failed() << '\n';
}

} // namespace queued
