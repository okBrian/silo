#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

namespace queued {

// A deliberately small software transport: one pending bit per destination.
// send() never delivers or admits work. The test schedule chooses deliver().
// Multiple sends before delivery coalesce; unsolicited delivery models a
// duplicate/spurious event. This does not emulate Intel/kernel instructions.
class NotificationModel {
public:
  struct Stats {
    uint64_t sends, coalesced, deliveries, unsolicited;
    bool pending;
  };
  using Deliver = std::function<void(size_t)>;

  NotificationModel(size_t workers, Deliver deliver)
    : slots(workers), receiver(std::move(deliver)) {}

  void send(size_t worker) {
    Slot &slot = slots.at(worker);
    ++slot.sends;
    if (slot.pending.exchange(true, std::memory_order_acq_rel))
      ++slot.coalesced;
  }

  bool deliver(size_t worker, bool unsolicited = false) {
    Slot &slot = slots.at(worker);
    const bool pending = slot.pending.exchange(false, std::memory_order_acq_rel);
    if (!pending && !unsolicited)
      return false;
    ++slot.deliveries;
    if (!pending)
      ++slot.unsolicited;
    receiver(worker);
    return true;
  }

  Stats stats(size_t worker) const {
    const Slot &slot = slots.at(worker);
    return {slot.sends.load(), slot.coalesced.load(), slot.deliveries.load(),
            slot.unsolicited.load(), slot.pending.load()};
  }

private:
  struct Slot {
    std::atomic<bool> pending{false};
    std::atomic<uint64_t> sends{0}, coalesced{0}, deliveries{0}, unsolicited{0};
  };
  std::deque<Slot> slots;
  const Deliver receiver;
};

} // namespace queued
