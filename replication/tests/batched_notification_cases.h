#pragma once

#include "../../benchmarks/request_driver.h"
#include "../../benchmarks/notification_model.h"
#include "../../preemption.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace notification_cases {

inline bool &signal_backend() { static bool enabled = false; return enabled; }
inline queued::Policy policy() { return signal_backend() ? queued::Policy::linux_signal : queued::Policy::notification_checkpoint; }

inline void check(bool value, int line) {
  if (!value) {
    std::cerr << "FAIL batched notification case at line " << line << '\n';
    std::abort();
  }
}
#define BATCH_CHECK(value) ::notification_cases::check((value), __LINE__)

using LowRun = std::function<bool(uint64_t &, const std::function<void()> &)>;
using HighCommit = std::function<void(const queued::Request &)>;

// Both the standalone driver and real Silo Q2 fixture run exactly this schedule.
// Worker execution stays on the caller thread to retain Silo's owning TLS/RCU.
inline void transition_case(queued::ServiceTransition target, unsigned mode,
                            const LowRun &run_low, const HighCommit &commit_high,
                            uint64_t expected_checksum, const char *label) {
  using namespace queued;
  Config config;
  config.policy = policy();
  config.low_kind = Kind::q2;
  config.interval = 10;
  config.batch_size = 2;
  config.high_capacity = config.global_capacity = 4;
  std::atomic<Ns> time{100};
  std::promise<void> started, initial, transition, later, finished;
  auto started_f = started.get_future();
  auto initial_f = initial.get_future();
  auto transition_f = transition.get_future();
  auto later_f = later.get_future();
  auto finished_f = finished.get_future();
  unsigned publications = 0, highs = 0, reads = 0, transitions = 0;
  bool initial_empty_checked = false;
  Driver *driver_ptr = nullptr;
  NotificationModel model(1, [&](size_t worker) { driver_ptr->deliver_notification(worker); });
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) started_f.wait();
    if (next == 120) transition_f.wait();
    if (next == 130) finished_f.wait();
    time = next;
  }, [&](size_t worker) {
    ++publications;
    model.send(worker);
    if (mode == 2) {
      model.send(worker); // Delayed, coalesced transport; queue still wins.
    } else {
      BATCH_CHECK(model.deliver(worker));
      if (mode == 1) BATCH_CHECK(model.deliver(worker, true));
    }
    (publications == 1 ? initial : later).set_value();
  }, [&](size_t worker, ServiceTransition point) {
    BATCH_CHECK(worker == 0);
    if (point == target && transitions++ == 0) {
      transition.set_value();
      later_f.wait(); // Publish precisely at the selected transition.
      if (mode == 2) BATCH_CHECK(model.deliver(worker));
    }
  });
  driver_ptr = &driver;
  // Empty/spurious delivery before low execution must not switch or create work.
  BATCH_CHECK(model.deliver(0, true));
  std::thread producer([&] { driver.produce(30, 1000); });
  driver.run_prepared_worker(0, [&](const Request &request) -> Driver::Prepared {
    if (request.batch) {
      return [&](uint64_t &) {
        BATCH_CHECK(!ScopedNonPreemptible::blocked());
        if (request.high_sequence == 0 && request.attempts == 1)
          return Attempt::conflict;
        commit_high(request);
        ++highs;
        return Attempt::committed;
      };
    }
    if (request.id != 0)
      return [&](uint64_t &checksum) {
        return run_low(checksum, [] {}) ? Attempt::committed : Attempt::failed;
      };
    return [&](uint64_t &checksum) {
      const bool committed = run_low(checksum, [&] {
        if (++reads == 1) {
          driver.on_record_read(0); // No high queue, despite the spurious hint.
          BATCH_CHECK(highs == 0);
          initial_empty_checked = true;
          started.set_value();
          initial_f.wait();
          {
            ScopedNonPreemptible outer;
            driver.on_record_read(0);
            {
              ScopedNonPreemptible inner;
              driver.on_record_read(0);
            }
            BATCH_CHECK(highs == 0);
          }
          BATCH_CHECK(highs == 0); // Unprotecting alone never switches.
        }
        driver.on_record_read(0);
      });
      BATCH_CHECK(committed && checksum == expected_checksum && highs == 4);
      finished.set_value();
      return Attempt::committed;
    };
  });
  producer.join();
  driver.wait_for_drain();
  std::vector<Request> records;
  driver.visit_records([&](const Request &r) { records.push_back(r); });
  std::sort(records.begin(), records.end(), [](const Request &a, const Request &b) {
    return a.id < b.id;
  });
  unsigned attempts = 0, high_records = 0;
  BATCH_CHECK(!driver.failed() && records.size() == driver.generated());
  for (size_t i = 0; i < records.size(); ++i) {
    const Request &r = records[i];
    BATCH_CHECK(r.id == i && r.outcome == Outcome::committed);
    BATCH_CHECK(r.generated <= r.admitted && r.admitted <= r.first_start &&
                r.first_start <= r.finished);
    if (r.batch) {
      ++high_records;
      attempts += r.attempts;
      BATCH_CHECK(r.first_start >= records[0].first_start &&
                  r.finished <= records[0].finished);
    }
  }
  const auto stats = driver.notification_stats(0);
  const auto transport = model.stats(0);
  const unsigned switches = target == ServiceTransition::high_entry ? 1 : 2;
  BATCH_CHECK(initial_empty_checked && reads >= 2 && high_records == 4 && attempts == 5);
  BATCH_CHECK(publications == 2 && stats.publication_passes == 2 && stats.admitted_high == 4);
  BATCH_CHECK(stats.deliveries == transport.deliveries + (signal_backend() ? publications : 0) && stats.deferrals == 2);
  BATCH_CHECK(!signal_backend() || stats.signal_captures > 0);
  BATCH_CHECK(stats.spurious_checks >= 1 && stats.queue_fallbacks >= 1 && !stats.pending_hint);
  BATCH_CHECK(stats.switches == switches && stats.high_entries == switches &&
              stats.empty_checks == switches && stats.low_resumes == switches);
  BATCH_CHECK(!transport.pending && !model.deliver(0));
  BATCH_CHECK(transport.sends == (mode == 2 ? 4 : 2));
  BATCH_CHECK(transport.coalesced == (mode == 2 ? 3 : 0));
  BATCH_CHECK(transport.deliveries == (mode == 0 ? 3 : mode == 1 ? 5 : 2));
  std::cout << "CASE " << label << " transition=" << static_cast<unsigned>(target)
            << " mode=" << mode << " clock=controlled_ns seed=" << config.seed
            << " publications=" << publications << " admitted=4 sends=" << transport.sends
            << " coalesced=" << transport.coalesced << " modeled_deliveries=" << transport.deliveries
            << " unsolicited=" << transport.unsolicited << " backend=" << (signal_backend() ? "linux-signal" : "checkpoint")
            << " real_handlers=" << stats.signal_handlers << " async_captures=" << stats.signal_captures << " deferrals="
            << stats.deferrals << " switches=" << stats.switches << " low_resumes="
            << stats.low_resumes << " hints_consumed=" << stats.hints_consumed
            << " queue_fallbacks=" << stats.queue_fallbacks << " spurious_checks="
            << stats.spurious_checks << " transport_pending=" << transport.pending
            << " scheduler_pending=" << stats.pending_hint << " high_attempts=5 high_commits=4 checksum="
            << expected_checksum << '\n';
  write_header(std::cout);
  write_results(driver, std::cout, std::cout, label, "controlled");
}

inline void transition_matrix(const LowRun &low, const HighCommit &high,
                              uint64_t checksum, const char *label) {
  for (auto point : {queued::ServiceTransition::high_entry,
                     queued::ServiceTransition::high_empty,
                     queued::ServiceTransition::before_low_resume})
    for (unsigned mode = 0; mode < 3; ++mode)
      transition_case(point, mode, low, high, checksum, label);
}

} // namespace notification_cases
#undef BATCH_CHECK
