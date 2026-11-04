#include "../../benchmarks/request_driver.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

using namespace queued;

static std::vector<Request> checked_records(const Driver &driver, uint64_t first_id = 0) {
  std::vector<Request> records;
  driver.visit_records([&](const Request &r) { records.push_back(r); });
  assert(records.size() == driver.generated());
  std::sort(records.begin(), records.end(),
            [](const Request &a, const Request &b) { return a.id < b.id; });
  for (size_t i = 0; i < records.size(); ++i) {
    const Request &r = records[i];
    assert(r.id == first_id + i);
    assert(r.outcome != Outcome::pending);
    assert(r.home_warehouse == r.worker + 1);
    assert(r.scheduled <= r.generated && r.generated <= r.finished);
    assert(r.aborts <= r.attempts);
    assert(bool(r.first_start) == bool(r.attempts));
    if (r.admitted)
      assert(r.generated <= r.admitted && r.admitted <= r.finished);
    if (r.first_start)
      assert(r.admitted && r.admitted <= r.first_start && r.first_start <= r.finished);
    if (r.outcome == Outcome::rejected_queue_full)
      assert(!r.admitted && !r.attempts && r.finished == r.generated);
  }
  return records;
}

static void late_batches_and_cancellation() {
  std::atomic<Ns> time{100};
  std::promise<void> started, release;
  auto active = started.get_future();
  auto stop = release.get_future();
  Config config;
  config.batch_size = 2;
  config.interval = 10;
  config.global_capacity = 2;
  config.high_capacity = 1;
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110) {
      active.wait();
      time = 135; // Miss three deadlines with the worker still busy.
    } else {
      time = next;
    }
  });
  std::thread worker([&] {
    driver.run_worker(0, [&](const Request &r) {
      assert(r.kind == Kind::synthetic_low);
      started.set_value();
      stop.wait();
      return Attempt::committed;
    });
  });
  driver.produce(50, 30);
  time = 181;
  release.set_value();
  worker.join();

  auto records = checked_records(driver);
  assert(driver.failed()); // Active work completed safely after the deadline.
  std::map<Outcome, unsigned> outcomes;
  unsigned high = 0, late = 0, cancelled_global = 0, cancelled_local = 0;
  for (const auto &r : records) {
    ++outcomes[r.outcome];
    if (r.batch) {
      ++high;
      assert(r.scheduled == 100 + r.batch * 10 && r.batch <= 4);
      assert(r.generated == (r.batch <= 3 ? 135 : 140));
      late += r.generated > r.scheduled;
    }
    if (r.outcome == Outcome::cancelled_shutdown) {
      assert(!r.attempts);
      r.admitted ? ++cancelled_local : ++cancelled_global;
    }
  }
  assert(records.size() == 10 && high == 8 && late == 6);
  assert(outcomes[Outcome::rejected_queue_full] == 5);
  assert(outcomes[Outcome::cancelled_shutdown] == 4);
  assert(outcomes[Outcome::committed] == 1);
  assert(cancelled_global == 2 && cancelled_local == 2);

  std::ostringstream rows, summary;
  write_header(rows);
  write_results(driver, rows, summary, "cancellation", "measurement");
  assert(summary.str().find("cutoff_pending_global=2 cutoff_queued=2 cutoff_running=1") !=
         std::string::npos);
  std::cout << "PASS delayed generation and overload: 8 high, 2 low, 5 rejected, "
               "4 cancelled, 1 safe completion after deadline\n";
  std::cout << summary.str();
}

static void retries_and_duplicate_notifications() {
  std::atomic<Ns> time{100};
  std::promise<void> all_started;
  auto final_attempt = all_started.get_future();
  Config config;
  config.generate_low = false;
  config.interval = 10;
  config.attempt_limit = 3;
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    driver.notify(0);
    driver.notify(0);
    if (next == 120)
      final_attempt.wait();
    time = next;
  });
  std::map<uint64_t, Request> inputs;
  std::thread worker([&] {
    driver.run_prepared_worker(0, [&](const Request &r) {
      const auto saved = inputs.emplace(r.id, r);
      assert(saved.second && r.attempts == 1);
      assert(r.high_sequence == 8 + r.id - 20);
      const Request first = saved.first->second;
      return [&, first](uint64_t &) {
        assert(r.kind == first.kind && r.input_seed == first.input_seed);
        assert(r.scheduled == first.scheduled && r.generated == first.generated);
        assert(r.admitted == first.admitted && r.first_start == first.first_start);
        assert(r.worker == first.worker && r.home_warehouse == first.home_warehouse);
        assert(r.high_sequence == first.high_sequence);
        assert(r.aborts + 1 == r.attempts && !r.finished);
        switch (r.id) {
        case 20: return r.attempts < 3 ? Attempt::conflict : Attempt::committed;
        case 21: return Attempt::conflict;
        case 22: return Attempt::user_aborted;
        case 23:
          assert(r.attempts == 1);
          all_started.set_value();
          return Attempt::committed;
        }
        std::abort();
      };
    });
  });
  driver.produce(20, 100, 20, 8);
  driver.wait_for_drain();
  worker.join();

  auto records = checked_records(driver, 20);
  assert(!driver.failed() && records.size() == 4 && inputs.size() == 4);
  assert(records[0].outcome == Outcome::committed && records[0].attempts == 3 &&
         records[0].aborts == 2);
  assert(records[1].outcome == Outcome::retry_exhausted && records[1].attempts == 3 &&
         records[1].aborts == 3);
  assert(records[2].outcome == Outcome::user_aborted && records[2].attempts == 1);
  assert(records[3].outcome == Outcome::committed && records[3].attempts == 1);
  std::cout << "PASS stable retries, retry exhaustion, user abort, FIFO and duplicate notifications\n";
}

static void late_past_cutoff() {
  std::atomic<Ns> time{100};
  Config config;
  config.generate_low = false;
  config.interval = 10;
  config.batch_size = 1;
  Driver driver(config, [&] { return time.load(); }, [&](Ns) { time = 200; });
  std::thread worker([&] {
    driver.run_worker(0, [&](const Request &) -> Attempt { std::abort(); });
  });
  driver.produce(50, 20);
  worker.join();

  auto records = checked_records(driver);
  assert(!driver.failed() && records.size() == 4);
  for (const auto &r : records) {
    assert(r.scheduled == 100 + r.batch * 10 && r.generated == 200);
    assert(r.outcome == Outcome::cancelled_shutdown && !r.admitted && !r.attempts);
  }
  std::ostringstream rows, summary;
  write_results(driver, rows, summary, "late-cutoff", "measurement");
  assert(summary.str().find("cutoff_due_ungenerated=4") != std::string::npos);
  std::cout << "PASS wake after drain deadline accounts for every pre-cutoff batch\n";
}

static void fatal_attempt() {
  Config config;
  config.generate_low = false;
  config.interval = 2 * ms;
  Driver driver(config);
  unsigned attempts = 0;
  std::thread worker([&] {
    driver.run_worker(0, [&](const Request &) -> Attempt {
      ++attempts;
      throw 1; // Synthetic adapter, with no transaction resources to unwind.
    });
  });
  driver.produce(100 * ms, second);
  worker.join();
  auto records = checked_records(driver);
  assert(driver.failed() && attempts == 1);
  assert(records.front().outcome == Outcome::failed);
  for (size_t i = 1; i < records.size(); ++i)
    assert(!records[i].attempts);
  std::cout << "PASS fatal adapter error stops new attempts and preserves terminal records\n";
}

static void wait_policy() {
  constexpr Ns us = 1000;
  Config config;
  config.batch_size = 2;
  config.interval = 10 * us;
  std::atomic<Ns> time{100 * us};
  std::promise<void> release, started, next_low;
  auto stop = release.get_future();
  auto active = started.get_future();
  auto drained_batch = next_low.get_future();
  Driver driver(config, [&] { return time.load(); }, [&](Ns next) {
    if (next == 110 * us) {
      active.wait(); // First low request is running before the batch arrives.
    } else if (next == 120 * us) {
      // Both high requests and the second low are now admitted.
      time = 117 * us;
      release.set_value();
      drained_batch.wait();
    }
    time = next;
  });
  std::vector<uint64_t> order;
  std::thread worker([&] {
    driver.run_worker(0, [&](const Request &r) {
      order.push_back(r.id);
      if (order.size() == 1) {
        assert(r.kind == Kind::synthetic_low);
        started.set_value();
        stop.wait();
      } else if (r.batch) {
        time += us;
      } else {
        assert(order.size() == 4);
        next_low.set_value();
      }
      return Attempt::committed;
    });
  });
  driver.produce(20 * us, second);
  worker.join();

  auto records = checked_records(driver);
  assert(!driver.failed() && records.size() == 4);
  assert((order == std::vector<uint64_t>{0, 1, 2, 3}));
  assert(!records[0].batch && records[0].first_start == 100 * us);
  assert(records[0].finished == 117 * us);
  for (size_t i = 1; i <= 2; ++i) {
    const Request &r = records[i];
    assert(r.batch == 1 && r.scheduled == 110 * us);
    assert(r.generated == r.scheduled && r.admitted == r.generated);
    assert(r.first_start == records[i - 1].finished);
    assert(r.first_start - r.admitted == (6 + i) * us);
    assert(r.finished - r.generated == (7 + i) * us);
  }
  assert(!records[3].batch && records[3].admitted == 110 * us);
  assert(records[3].first_start == records[2].finished);
  std::cout << "PASS Wait: running low finishes, two shorts start in FIFO order "
               "after 7 and 8 us of local queueing, then queued low starts\n";
}

static void multiworker_reconciliation() {
  Config config;
  config.workers = 3;
  config.interval = 100000;
  config.batch_size = 8;
  config.global_capacity = 8;
  Driver driver(config);
  std::vector<std::thread> workers;
  for (size_t worker = 0; worker < config.workers; ++worker)
    workers.emplace_back([&, worker] {
      driver.run_worker(worker, [&](const Request &r) {
        assert(r.worker == worker);
        return Attempt::committed;
      });
    });
  driver.produce(30 * ms, second);
  for (auto &worker : workers)
    worker.join();

  auto records = checked_records(driver);
  size_t high = 0, committed = 0, rejected = 0;
  for (const auto &r : records) {
    if (r.batch) {
      assert(r.worker == high % config.workers);
      assert(r.batch == high / config.batch_size + 1);
      ++high;
    }
    if (r.outcome == Outcome::committed)
      ++committed;
    else if (r.outcome == Outcome::rejected_queue_full)
      ++rejected;
  }
  assert(!driver.failed() && high == 299 * config.batch_size);
  assert(committed > 0 && committed + rejected == records.size());
  std::cout << "PASS three-worker routing and ID reconciliation: high=" << high
            << " generated=" << records.size() << " committed=" << committed
            << " rejected=" << rejected << '\n';
}

int main() {
  late_batches_and_cancellation();
  late_past_cutoff();
  retries_and_duplicate_notifications();
  fatal_attempt();
  wait_policy();
  multiworker_reconciliation();
}
