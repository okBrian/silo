#include "notification_model_cases.h"

int main() {
  using namespace queued;
  using notification_model_cases::check;
  using K = NotificationSchedule::Kind;
  unsigned received[2] = {};
  NotificationModel model(2, [&](size_t w) { ++received[w]; });
  NotificationSchedule schedule(model, {
    {1, K::deliver, 0}, {2, K::send, 0}, {3, K::send, 0},
    {3, K::send, 1}, {4, K::deliver, 1}, {5, K::deliver, 0},
    {5, K::unsolicited, 0}, {6, K::deliver, 1}, {6, K::send, 1},
    {7, K::close, 1}, {8, K::deliver, 1}, {8, K::send, 1},
    {8, K::unsolicited, 1}, {9, K::close, 0}, {9, K::close, 1}});
  schedule.run_through(1);
  check(!received[0] && !received[1], __LINE__);
  schedule.run_through(3);
  check(model.stats(0).pending && model.stats(1).pending && !received[0], __LINE__);
  schedule.run_through(4);
  check(received[1] == 1 && received[0] == 0 && model.stats(0).pending, __LINE__);
  schedule.run_through(9);
  const auto a = model.stats(0), b = model.stats(1);
  check(schedule.complete() && received[0] == 2 && received[1] == 1, __LINE__);
  check(a.sends == 2 && a.coalesced == 1 && a.deliveries == 2 && a.unsolicited == 1 && a.delivery_attempts == 3, __LINE__);
  check(b.sends == 3 && b.deliveries == 1 && b.cancelled == 1 && b.dropped_sends == 1 && b.dropped_deliveries == 2, __LINE__);
  check(a.closed && b.closed && !a.pending && !b.pending, __LINE__);
  notification_model_cases::trace(schedule, "transport");
  std::cout << "PASS transport routing, coalescing, no-op, equal-time ordering, pending teardown\n";

  // Delivery consumes the old bit before invoking the receiver. A send from
  // that receiver must remain pending for a second delivery.
  NotificationModel *ptr = nullptr;
  unsigned callbacks = 0;
  NotificationModel rearm(1, [&](size_t w) { if (++callbacks == 1) ptr->send(w); });
  ptr = &rearm;
  NotificationSchedule rearmed(rearm, {{1, K::send, 0}, {2, K::deliver, 0}, {3, K::deliver, 0}});
  rearmed.run_through(2);
  check(callbacks == 1 && rearm.stats(0).pending, __LINE__);
  rearmed.run_through(3);
  check(callbacks == 2 && !rearm.stats(0).pending && !rearm.stats(0).coalesced, __LINE__);
  rearm.close(0);
  notification_model_cases::trace(rearmed, "rearm");
  std::cout << "PASS arrival during receiver callback remains pending\n";

  unsigned rejected = 0;
  try { NotificationSchedule invalid(model, {{2, K::send, 0}, {1, K::send, 0}}); }
  catch (const std::invalid_argument &) { ++rejected; }
  try { NotificationSchedule invalid(model, {{1, K::send, 2}}); }
  catch (const std::invalid_argument &) { ++rejected; }
  try { schedule.run_through(8); }
  catch (const std::invalid_argument &) { ++rejected; }
  check(rejected == 3, __LINE__);
  std::cout << "PASS invalid schedules and backward clock rejected\n";

#ifdef SILO_WORKER_CONTEXTS
  for (bool delayed : {false, true})
    notification_model_cases::receiver_case(delayed,
      [](uint64_t &sum, const std::function<void()> &read) {
        sum = 0;
        for (unsigned i = 0; i < 18; ++i) { sum += i; read(); }
        return true;
      }, [](const Request &) {}, 153, "model-receiver");
#endif
}
