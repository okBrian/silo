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
    uint64_t delivery_attempts, dropped_sends, dropped_deliveries, cancelled;
    bool closed;
  };
  using Deliver = std::function<void(size_t)>;

  NotificationModel(size_t workers, Deliver deliver)
    : slots(workers), receiver(std::move(deliver)) {}

  void send(size_t worker) {
    Slot &slot = slots.at(worker);
    ++slot.sends;
    if (slot.closed.load()) { ++slot.dropped_sends; return; }
    if (slot.pending.exchange(true, std::memory_order_acq_rel))
      ++slot.coalesced;
  }

  bool deliver(size_t worker, bool unsolicited = false) {
    Slot &slot = slots.at(worker);
    ++slot.delivery_attempts;
    if (slot.closed.load()) { ++slot.dropped_deliveries; return false; }
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
            slot.unsolicited.load(), slot.pending.load(), slot.delivery_attempts.load(),
            slot.dropped_sends.load(), slot.dropped_deliveries.load(),
            slot.cancelled.load(), slot.closed.load()};
  }

  // Quiescent teardown: join senders/deliverers before close(), and close every
  // destination before destroying the receiver. This is NOT a concurrent
  // unregister/drain primitive. Later scheduled events are counted and dropped.
  void close(size_t worker) {
    Slot &slot = slots.at(worker);
    slot.closed = true;
    if (slot.pending.exchange(false)) ++slot.cancelled;
  }

  size_t worker_count() const { return slots.size(); }

private:
  struct Slot {
    std::atomic<bool> pending{false};
    std::atomic<uint64_t> sends{0}, coalesced{0}, deliveries{0}, unsolicited{0};
    std::atomic<uint64_t> delivery_attempts{0}, dropped_sends{0}, dropped_deliveries{0}, cancelled{0};
    std::atomic<bool> closed{false};
  };
  std::deque<Slot> slots;
  const Deliver receiver;
};

} // namespace queued
