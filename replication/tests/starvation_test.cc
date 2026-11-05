#include "starvation_cases.h"
int main() {
  for (unsigned threshold : {0u, 100u})
    starvation_cases::limits(threshold, [](uint64_t &sum, const std::function<void()> &read) {
      sum = 0;
      for (unsigned i = 1; i <= 18; ++i) { sum += i; read(); }
      return true;
    }, [](const queued::Request &) {}, 171, "standalone-limits");
  for (bool expire : {false, true})
    starvation_cases::controlled([](uint64_t &sum, const std::function<void()> &read) {
      sum = 0;
      for (unsigned i = 1; i <= 18; ++i) { sum += i; read(); }
      return true;
    }, [](const queued::Request &) {}, 171, expire, "standalone-starvation");
}
