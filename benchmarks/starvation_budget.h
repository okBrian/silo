#ifndef SILO_STARVATION_BUDGET_H
#define SILO_STARVATION_BUDGET_H

#include <cstdint>

namespace queued {

// Worker-owned accounting. The worker resets this once per low request and
// charges each entire high request, including retries, at its end. A producer
// reading a copy will need synchronized publication of the whole snapshot.
struct StarvationBudget {
  bool low_active = false;
  uint64_t low_start = 0;
  uint64_t high_cycles = 0;

  void begin_low(uint64_t now) {
    low_active = true;
    low_start = now;
    high_cycles = 0;
  }

  void end_low() { low_active = false; }

  void charge_high(uint64_t start, uint64_t finish) {
    if (low_active)
      high_cycles += finish - start;
  }

  // Both decisions use this predicate. Callers supply a common cycle clock,
  // nonoverlapping high intervals, and a threshold in [0, 100].
  bool starved(uint64_t now, unsigned threshold) const {
    if (!low_active || now <= low_start)
      return false;
    const uint64_t elapsed = now - low_start;
    const uint64_t allowed = (elapsed / 100) * threshold +
                             ((elapsed % 100) * threshold) / 100;
    return high_cycles > allowed;
  }
};

} // namespace queued

#endif
