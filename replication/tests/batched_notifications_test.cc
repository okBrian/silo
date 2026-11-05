#include "batched_notification_cases.h"

static void partial_admission() {
  using namespace queued;
  using notification_cases::check;
  Config config;
  config.workers = 2;
  config.batch_size = 6;
  config.global_capacity = 4;
  config.high_capacity = 2;
  config.interval = 10;
  std::atomic<Ns> time{100};
  std::promise<void> started[2], drained[2], release_low, release_high;
  std::future<void> started_f[2] = {started[0].get_future(), started[1].get_future()};
  std::future<void> drained_f[2] = {drained[0].get_future(), drained[1].get_future()};
  auto low_f = release_low.get_future().share();
  auto high_f = release_high.get_future().share();
  unsigned publications[2] = {}, served[2] = {};
  Driver *driver_ptr = nullptr;
  NotificationModel model(2, [&](size_t worker) { driver_ptr->deliver_notification(worker); });
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110)
      for (auto &f : started_f) f.wait();
    if (next == 130) {
      release_low.set_value();
      for (auto &f : drained_f) f.wait();
    }
    time = next;
  }, [&](size_t worker) {
    ++publications[worker];
    model.send(worker);
    model.send(worker);
    if (publications[worker] == 2) {
      check(model.deliver(worker), __LINE__);
      check(model.deliver(worker, true), __LINE__);
      if (worker == 1) release_high.set_value();
    }
  });
  driver_ptr = &driver;
  std::vector<std::thread> workers;
  for (size_t w = 0; w < 2; ++w) workers.emplace_back([&, w] {
    driver.run_worker(w, [&](const Request &r) {
      if (!r.batch && !served[w]) {
        started[w].set_value();
        low_f.wait();
      }
      if (r.batch) {
        if (r.high_sequence == 0 && r.attempts == 1) return Attempt::conflict;
        if (++served[w] == 2) {
          drained[w].set_value();
          high_f.wait();
        }
      }
      return Attempt::committed;
    });
  });
  driver.produce(30, 1000);
  for (auto &worker : workers) worker.join();
  driver.wait_for_drain();
  std::vector<Request> records;
  driver.visit_records([&](const Request &r) { records.push_back(r); });
  std::sort(records.begin(), records.end(), [](const Request &a, const Request &b) {
    return a.id < b.id;
  });
  unsigned commits = 0, rejected = 0, attempts = 0;
  check(!driver.failed() && records.size() == driver.generated(), __LINE__);
  for (size_t i = 0; i < records.size(); ++i) {
    const auto &r = records[i];
    check(r.id == i && r.outcome != Outcome::pending, __LINE__);
    if (!r.batch) { check(r.outcome == Outcome::committed, __LINE__); continue; }
    check(r.worker == r.high_sequence % 2, __LINE__);
    attempts += r.attempts;
    if (r.outcome == Outcome::rejected_queue_full) {
      ++rejected;
      check(!r.admitted && !r.attempts, __LINE__);
    } else {
      ++commits;
      check(r.outcome == Outcome::committed && r.admitted, __LINE__);
    }
  }
  check(driver.high_generated() == 12 && commits == 8 && rejected == 4 && attempts == 9, __LINE__);
  for (size_t w = 0; w < 2; ++w) {
    auto stats = driver.notification_stats(w);
    auto transport = model.stats(w);
    check(publications[w] == 2 && served[w] == 4, __LINE__);
    check(stats.publication_passes == 2 && stats.admitted_high == 4 &&
          stats.deliveries == 2 && stats.switches == 0, __LINE__);
    check(transport.sends == 4 && transport.coalesced == 3 &&
          transport.deliveries == 2 && transport.unsolicited == 1 && !transport.pending, __LINE__);
  }
  std::cout << "CASE partial-admission clock=controlled_ns seed=1 workers=2 high_capacity=2"
               " global_capacity=4 batch=6 high_generated=12 admitted=8 rejected=4"
               " publications=4 sends=8 coalesced=6 modeled_deliveries=4 real_handlers=0"
               " switches=0 high_attempts=9 high_commits=8\n";
  write_header(std::cout);
  write_results(driver, std::cout, std::cout, "partial-admission", "controlled");
}

static void drain_expiry() {
  using namespace queued;
  using notification_cases::check;
  Config config;
  config.interval = 10;
  config.batch_size = config.global_capacity = 4;
  config.high_capacity = 1;
  std::atomic<Ns> time{100};
  std::promise<void> started, release;
  auto started_f = started.get_future();
  auto release_f = release.get_future();
  Driver *driver_ptr = nullptr;
  NotificationModel model(1, [&](size_t w) { driver_ptr->deliver_notification(w); });
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) { started_f.wait(); time = next; }
    else { time = 130; release.set_value(); }
  }, [&](size_t w) { model.send(w); });
  driver_ptr = &driver;
  unsigned executions = 0;
  std::thread worker([&] {
    driver.run_worker(0, [&](const Request &r) {
      check(r.id == 0 && ++executions == 1, __LINE__);
      started.set_value();
      release_f.wait();
      return Attempt::committed;
    });
  });
  driver.produce(20, 10);
  worker.join();
  driver.wait_for_drain();
  unsigned committed = 0, cancelled = 0, admitted_high = 0;
  std::vector<uint64_t> ids;
  driver.visit_records([&](const Request &r) {
    ids.push_back(r.id);
    if (r.outcome == Outcome::committed) ++committed;
    else { check(r.outcome == Outcome::cancelled_shutdown, __LINE__); ++cancelled; }
    if (r.batch && r.admitted) ++admitted_high;
  });
  std::sort(ids.begin(), ids.end());
  check(ids.size() == driver.generated(), __LINE__);
  for (size_t i = 0; i < ids.size(); ++i) check(ids[i] == i, __LINE__);
  check(!driver.failed() && executions == 1 && committed == 1 && cancelled == 5 &&
        admitted_high == 1, __LINE__);
  check(model.stats(0).pending && !model.stats(0).deliveries, __LINE__);
  // A delayed delivery after join is a harmless stale hint while Driver lives.
  check(model.deliver(0) && !model.deliver(0), __LINE__);
  check(driver.notification_stats(0).deliveries == 1 &&
        driver.notification_stats(0).switches == 0, __LINE__);
  std::cout << "CASE drain-expiry clock=controlled_ns seed=1 high_generated=4 admitted=1"
               " cancelled_high=4 cancelled_low=1 safe_low_commits=1 sends=1"
               " modeled_deliveries=1 delivery_time=after_join real_handlers=0 switches=0\n";
  write_header(std::cout);
  write_results(driver, std::cout, std::cout, "drain-expiry", "controlled");
}

int main() {
  partial_admission();
  drain_expiry();
#ifdef SILO_WORKER_CONTEXTS
  notification_cases::transition_matrix([](uint64_t &checksum,
                                            const std::function<void()> &read) {
    uint64_t sum = 0;
    for (unsigned i = 0; i < 18; ++i) {
      sum += i;
      read();
    }
    checksum = sum;
    return true;
  }, [](const queued::Request &) {}, 153, "synthetic-q2");
  std::cout << "PASS nine modeled notification transition cases (checkpoint service)\n";
#else
  std::cout << "PASS two-worker partial admission, bounded drain and modeled transport (Wait build)\n";
#endif
}
