#include "../../benchmarks/request_driver.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
int main() {
  queued::Config config;
  config.workers = 2;
  config.interval = queued::ms;
  queued::Driver driver(config);
  std::atomic<bool> done{false};
  unsigned snapshots = 0;
  std::thread reader([&] {
    uint64_t previous[2] = {};
    while (!done.load()) {
      for (size_t w = 0; w < 2; ++w) {
        const auto b = driver.budget_snapshot(w);
        assert(b.low_start >= previous[w] && b.high_cycles == 0);
        assert(!b.low_active || b.low_start > 0);
        previous[w] = b.low_start;
        ++snapshots;
      }
    }
  });
  std::thread producer([&] { driver.produce(30 * queued::ms, queued::second); });
  std::thread workers[2];
  for (size_t w = 0; w < 2; ++w) workers[w] = std::thread([&, w] {
    driver.run_worker(w, [](const queued::Request &) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
      return queued::Attempt::committed;
    });
  });
  producer.join();
  for (auto &w : workers) w.join();
  done = true;
  reader.join();
  assert(!driver.failed() && snapshots > 0);
  std::cout << "PASS coherent concurrent budget snapshots=" << snapshots << " workers=2\n";
}
