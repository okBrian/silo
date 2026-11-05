#pragma once
#include "batched_notification_cases.h"

namespace starvation_cases {
using notification_cases::LowRun;
using notification_cases::HighCommit;
#define STARVE_CHECK(value) ::notification_cases::check((value), __LINE__)

// Four queued highs plus four more at cutoff create real admission pressure.
// A 50% budget allows equality, overshoots on a whole high, and then recovers.
inline void controlled(const LowRun &run_low, const HighCommit &commit_high,
                       uint64_t oracle, bool expire, const char *label) {
  using namespace queued;
  Config config;
  config.policy = notification_cases::policy();
  config.low_kind = Kind::q2;
  config.starvation_threshold = 50;
  config.interval = 10;
  config.batch_size = config.high_capacity = config.global_capacity = 4;
  std::atomic<Ns> time{100};
  const auto advance = [&](Ns to) {
    Ns old = time.load();
    while (old < to && !time.compare_exchange_weak(old, to)) {}
  };
  std::promise<void> started, initial, overrun, checked, recovery, refill, done, producer_done;
  auto started_f = started.get_future();
  auto initial_f = initial.get_future();
  auto overrun_f = overrun.get_future();
  auto checked_f = checked.get_future();
  auto recovery_f = recovery.get_future();
  auto refill_f = refill.get_future();
  auto done_f = done.get_future().share();
  auto producer_done_f = producer_done.get_future();
  unsigned passes = 0, waits = 0, high_commits = 0, reads = 0, low_attempts = 0;
  bool exact = false, retained = false;
  Driver *ptr = nullptr;
  NotificationModel model(1, [&](size_t w) { ptr->deliver_notification(w); });
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) { started_f.wait(); advance(110); }
    else if (next == 120) { overrun_f.wait(); advance(130); }
    else if (waits++ == 0) {
      // The producer has just observed the coherent over-budget snapshot.
      checked.set_value();
      recovery_f.wait();
      advance(140);
    } else {
      done_f.wait();
      if (expire) advance(1130);
      else { advance(500); std::this_thread::yield(); }
    }
  }, [&](size_t w) {
    model.send(w);
    STARVE_CHECK(model.deliver(w));
    if (++passes == 1) initial.set_value();
    if (passes == 2) refill.set_value();
  });
  ptr = &driver;
  std::thread producer([&] { driver.produce(30, 1000); producer_done.set_value(); });
  driver.run_prepared_worker(0, [&](const Request &request) -> Driver::Prepared {
    if (request.batch) return [&](uint64_t &) {
      if (request.high_sequence == 0) {
        time.fetch_add(5);
        if (request.attempts == 1) return Attempt::conflict;
      } else time.fetch_add(10);
      if (request.high_sequence == 1) {
        const auto b = driver.budget_snapshot(0);
        STARVE_CHECK(b.low_active && b.low_start == 100 && b.high_cycles == 10);
        STARVE_CHECK(!b.starved(120, 50));
        exact = true; // The second high was actually dispatched at exact equality.
      }
      commit_high(request);
      ++high_commits;
      return Attempt::committed;
    };
    if (request.id != 0) return [&](uint64_t &checksum) {
      time.fetch_add(5);
      return run_low(checksum, [] {}) ? Attempt::committed : Attempt::failed;
    };
    return [&](uint64_t &checksum) {
      ++low_attempts;
      if (low_attempts == 2) {
        const auto b = driver.budget_snapshot(0);
        STARVE_CHECK(b.low_active && b.low_start == 100 && b.high_cycles == 60);
        retained = true;
        time.fetch_add(5);
        STARVE_CHECK(run_low(checksum, [] {}) && checksum == oracle);
        done.set_value();
        producer_done_f.wait();
        return Attempt::committed;
      }
      STARVE_CHECK(run_low(checksum, [&] {
        if (++reads == 1) {
          started.set_value();
          initial_f.wait();
          driver.on_record_read(0);
          STARVE_CHECK(high_commits == 2 && time == 130);
          const auto b = driver.budget_snapshot(0);
          STARVE_CHECK(b.high_cycles == 20 && b.starved(130, 50));
          overrun.set_value();
          checked_f.wait();
          advance(139);
          driver.on_record_read(0);
          STARVE_CHECK(high_commits == 2); // Still over budget: no handoff.
          advance(140);
          recovery.set_value();
          refill_f.wait();
          driver.on_record_read(0); // Equality recovers admission and service.
          STARVE_CHECK(high_commits == 3);
        } else {
          time.fetch_add(10);
          driver.on_record_read(0);
        }
      }) && checksum == oracle);
      STARVE_CHECK(high_commits == 6);
      return Attempt::conflict; // Controlled low retry: same budget/request/input.
    };
  });
  producer.join();
  driver.wait_for_drain();
  const auto stats = driver.starvation_stats(0);
  const auto budget = driver.budget_snapshot(0);
  const auto notifications = driver.notification_stats(0);
  std::vector<Request> records;
  driver.visit_records([&](const Request &r) { records.push_back(r); });
  std::sort(records.begin(), records.end(), [](const Request &a, const Request &b) { return a.id < b.id; });
  unsigned committed = 0, cancelled = 0, highs = 0, low_commits = 0;
  for (size_t i = 0; i < records.size(); ++i) {
    const auto &r = records[i];
    STARVE_CHECK(r.id == i && r.outcome != Outcome::pending);
    if (r.outcome == Outcome::committed) {
      ++committed;
      STARVE_CHECK(r.admitted <= r.first_start && r.first_start <= r.finished);
      if (r.batch) ++highs;
      else { ++low_commits; STARVE_CHECK(r.q2_checksum == oracle && r.low_service_ns > 0); }
    } else { STARVE_CHECK(r.outcome == Outcome::cancelled_shutdown); ++cancelled; }
  }
  STARVE_CHECK(!driver.failed() && records.size() == driver.generated() && driver.high_generated() == 8);
  STARVE_CHECK(exact && retained && stats.admission_blocks >= 1 && stats.checkpoint_blocks >= 1);
  STARVE_CHECK(stats.forced_low_returns == 5 && stats.high_service_ns == 60 && stats.low_service_ns > 0);
  STARVE_CHECK(records[0].attempts == 2 && records[0].aborts == 1 && records[0].outcome == Outcome::committed);
  STARVE_CHECK(!budget.low_active && (expire || (budget.high_cycles == 0 && stats.low_requests >= 2)));
  STARVE_CHECK(highs == (expire ? 6 : 8) && low_commits >= 1 && cancelled == (expire ? 3 : 0));
  std::cout << "CASE " << label << " expire=" << expire << " threshold=50 clock=controlled_ns seed=1"
            << " exact_threshold_dispatch=1 retry_retained=1 high_generated=8 high_commits=" << highs
            << " q2_commits=" << low_commits << " cancelled=" << cancelled
            << " forced_returns=" << stats.forced_low_returns << " low_service_ns=" << stats.low_service_ns
            << " high_service_ns=" << stats.high_service_ns << " switches=" << notifications.switches
            << " modeled_deliveries=" << model.stats(0).deliveries << " real_handlers=" << notifications.signal_handlers << " async_captures=" << notifications.signal_captures << " checksum=" << oracle << '\n';
  write_header(std::cout);
  write_results(driver, std::cout, std::cout, label, "controlled");
}
#undef STARVE_CHECK
} // namespace starvation_cases

namespace starvation_cases {
inline void limits(unsigned threshold, const LowRun &run_low, const HighCommit &commit_high,
                   uint64_t oracle, const char *label) {
  using namespace queued;
  Config config;
  config.policy = notification_cases::policy();
  config.low_kind = Kind::q2;
  config.starvation_threshold = threshold;
  config.batch_size = config.high_capacity = config.global_capacity = 4;
  config.interval = 10;
  std::atomic<Ns> time{100};
  std::promise<void> started, published, done, closed;
  auto started_f = started.get_future();
  auto published_f = published.get_future();
  auto done_f = done.get_future();
  auto closed_f = closed.get_future();
  unsigned highs = 0, active_highs = 0, reads = 0;
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) { started_f.wait(); time = 110; }
    else { done_f.wait(); time = 500; }
  }, [&](size_t) { published.set_value(); });
  std::thread producer([&] { driver.produce(20, 1000); closed.set_value(); });
  driver.run_prepared_worker(0, [&](const Request &r) -> Driver::Prepared {
    if (r.batch) return [&](uint64_t &) {
      time.fetch_add(10);
      commit_high(r);
      ++highs;
      return Attempt::committed;
    };
    return [&](uint64_t &checksum) {
      const bool ok = run_low(checksum, [&] {
        if (r.id == 0) {
          if (++reads == 1) { started.set_value(); published_f.wait(); }
          time.fetch_add(10);
          driver.on_record_read(0);
        }
      });
      notification_cases::check(ok && checksum == oracle, __LINE__);
      if (r.id == 0) {
        active_highs = highs;
        done.set_value();
        closed_f.wait();
      }
      time.fetch_add(r.id == 0 ? 0 : 5);
      return Attempt::committed;
    };
  });
  producer.join();
  driver.wait_for_drain();
  const auto stats = driver.starvation_stats(0);
  notification_cases::check(!driver.failed() && highs == 4 && active_highs == (threshold ? 4 : 1), __LINE__);
  notification_cases::check(stats.forced_low_returns == (threshold ? 0 : 1) &&
                            (threshold || stats.checkpoint_blocks > 0), __LINE__);
  const auto b = driver.budget_snapshot(0);
  notification_cases::check(!b.low_active && b.high_cycles == 0 && stats.low_requests == 2, __LINE__);
  std::cout << "CASE " << label << " threshold=" << threshold << " clock=controlled_ns seed=1 active_highs="
            << active_highs << " high_commits=" << highs << " forced_returns=" << stats.forced_low_returns
            << " low_service_ns=" << stats.low_service_ns << " real_handlers=" << notifications.signal_handlers << " async_captures=" << notifications.signal_captures << " checksum=" << oracle << '\n';
  write_header(std::cout);
  write_results(driver, std::cout, std::cout, label, "controlled");
}
} // namespace starvation_cases
