#pragma once

#include "notification_model.h"
#include <stdexcept>
#include <vector>

namespace queued {

// A single-coordinator logical clock. Caller-provided order breaks equal-time
// ties, so delivery-before-send differs observably from send-before-delivery.
// The coordinator may move between threads only through external synchronization.
// Nothing runs on wall time, in a signal handler, or on a background thread.
class NotificationSchedule {
public:
  enum class Kind { send, deliver, unsolicited, close };
  struct Event { uint64_t at; Kind kind; size_t worker; };
  struct Trace { Event event; bool delivered; NotificationModel::Stats after; };

  NotificationSchedule(NotificationModel &transport, std::vector<Event> events)
    : model(transport), events(std::move(events)) {
    uint64_t previous = 0;
    for (const auto &event : this->events) {
      if (event.at < previous || event.worker >= model.worker_count())
        throw std::invalid_argument("invalid notification event schedule");
      previous = event.at;
    }
  }

  void run_through(uint64_t now) {
    if (now < clock) throw std::invalid_argument("notification clock moved backwards");
    clock = now;
    while (next < events.size() && events[next].at <= now) {
      const Event event = events[next++];
      bool delivered = false;
      switch (event.kind) {
      case Kind::send: model.send(event.worker); break;
      case Kind::deliver: delivered = model.deliver(event.worker); break;
      case Kind::unsolicited: delivered = model.deliver(event.worker, true); break;
      case Kind::close: model.close(event.worker); break;
      }
      history.push_back({event, delivered, model.stats(event.worker)});
    }
  }

  bool complete() const { return next == events.size(); }
  const std::vector<Trace> &trace() const { return history; }

private:
  NotificationModel &model;
  const std::vector<Event> events;
  size_t next = 0;
  uint64_t clock = 0;
  std::vector<Trace> history;
};

} // namespace queued
