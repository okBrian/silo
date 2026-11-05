#pragma once
#include "batched_notification_cases.h"
#include "../../benchmarks/notification_schedule.h"

namespace notification_model_cases {
using notification_cases::check;
using notification_cases::LowRun;
using notification_cases::HighCommit;

inline void trace(const queued::NotificationSchedule &schedule, const char *label) {
  for (const auto &entry : schedule.trace()) {
    const auto &s = entry.after;
    std::cout << "EVENT " << label << " at=" << entry.event.at
              << " kind=" << static_cast<unsigned>(entry.event.kind)
              << " worker=" << entry.event.worker << " delivered=" << entry.delivered
              << " sends=" << s.sends << " coalesced=" << s.coalesced
              << " deliveries=" << s.deliveries << " unsolicited=" << s.unsolicited
              << " pending=" << s.pending << " closed=" << s.closed
              << " delivery_attempts=" << s.delivery_attempts
              << " dropped_sends=" << s.dropped_sends
              << " dropped_deliveries=" << s.dropped_deliveries
              << " cancelled=" << s.cancelled << '\n';
  }
}

// A hint is deliberately withheld until after high work completed, or delivered
// before nested protection. Both schedules must resume the exact same low
// attempt. The queue, rather than the notification bit, authorizes service.
inline void receiver_case(bool delayed, const LowRun &run_low,
                          const HighCommit &commit_high, uint64_t oracle,
                          const char *label) {
  using namespace queued;
  using K = NotificationSchedule::Kind;
  Config config;
  config.policy = Policy::notification_checkpoint;
  config.low_kind = Kind::q2;
  config.interval = 10;
  config.batch_size = config.high_capacity = config.global_capacity = 2;
  std::atomic<Ns> time{100};
  std::promise<void> started, published, done, closed;
  auto started_f = started.get_future();
  auto published_f = published.get_future();
  auto done_f = done.get_future();
  auto closed_f = closed.get_future();
  unsigned highs = 0, reads = 0, publications = 0, callbacks = 0;
  std::unique_ptr<Driver> driver;
  NotificationModel model(1, [&](size_t w) {
    check(bool(driver), __LINE__);
    ++callbacks;
    driver->deliver_notification(w);
  });
  std::vector<NotificationSchedule::Event> events{{110, K::send, 0}, {110, K::send, 0}};
  if (!delayed) events.push_back({110, K::deliver, 0});
  events.insert(events.end(), {{120, K::deliver, 0}, {120, K::unsolicited, 0},
    {130, K::close, 0}, {140, K::deliver, 0}, {140, K::send, 0}, {150, K::unsolicited, 0}});
  NotificationSchedule schedule(model, std::move(events));
  driver.reset(new Driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) { started_f.wait(); time = next; }
    else { done_f.wait(); time = 130; }
  }, [&](size_t w) {
    check(w == 0 && ++publications == 1, __LINE__);
    schedule.run_through(110);
    published.set_value();
  }));
  std::thread producer([&] { driver->produce(20, 1000); closed.set_value(); });
  driver->run_prepared_worker(0, [&](const Request &request) -> Driver::Prepared {
    if (request.batch) return [&](uint64_t &) {
      if (request.high_sequence == 0 && request.attempts == 1) return Attempt::conflict;
      commit_high(request);
      ++highs;
      return Attempt::committed;
    };
    return [&](uint64_t &checksum) {
      check(run_low(checksum, [&] {
        if (request.id == 0 && ++reads == 1) {
          started.set_value();
          published_f.wait();
          {
            ScopedNonPreemptible outer;
            driver->on_record_read(0);
            { ScopedNonPreemptible inner; driver->on_record_read(0); }
            check(highs == 0, __LINE__);
          }
          check(highs == 0, __LINE__);
          driver->on_record_read(0);
          check(highs == 2, __LINE__);
          time = 120;
          schedule.run_through(120); // Stale/duplicate events, authoritative queue empty.
          driver->on_record_read(0);
          check(highs == 2, __LINE__);
        }
      }) && checksum == oracle, __LINE__);
      if (request.id == 0) { done.set_value(); closed_f.wait(); }
      return Attempt::committed;
    };
  });
  producer.join();
  driver->wait_for_drain();
  unsigned high_records = 0, attempts = 0, count = 0;
  driver->visit_records([&](const Request &r) {
    ++count;
    check(r.outcome == Outcome::committed && r.attempts == r.aborts + 1, __LINE__);
    if (r.batch) { ++high_records; attempts += r.attempts; }
    else check(r.q2_checksum == oracle && r.attempts == 1, __LINE__);
  });
  const auto stats = driver->notification_stats(0);
  check(!driver->failed() && count == driver->generated() && high_records == 2 && attempts == 3, __LINE__);
  check(stats.admitted_high == 2 && stats.publication_passes == 1 && stats.deliveries == 2, __LINE__);
  check(stats.deferrals == 2 && stats.switches == 1 && stats.low_resumes == 1 &&
        stats.queue_fallbacks >= 1 && stats.spurious_checks == 1 && !stats.pending_hint &&
        stats.signal_handlers == 0 && stats.signal_captures == 0, __LINE__);
  std::cout << "CASE " << label << " delayed=" << delayed << " clock=controlled_ns seed=1"
            << " backend=checkpoint high_commits=2 high_attempts=3 publications=1"
            << " real_handlers=0 async_captures=0 switches=1 low_resumes=1 deferrals=2 checksum=" << oracle << '\n';
  write_header(std::cout);
  write_results(*driver, std::cout, std::cout, label, "controlled");
  schedule.run_through(130);
  driver.reset(); // Model deliberately outlives receiver; no further callbacks permitted.
  schedule.run_through(150);
  const auto s = model.stats(0);
  check(schedule.complete() && callbacks == 2 && s.sends == 3 && s.coalesced == 1 &&
        s.deliveries == 2 && s.unsolicited == 1 &&
        !s.pending && s.closed && s.dropped_sends == 1 && s.dropped_deliveries == 2, __LINE__);
  trace(schedule, label);
  std::cout << "PASS receiver schedule delayed=" << delayed << " callbacks_after_destroy=0\n";
}
} // namespace notification_model_cases
