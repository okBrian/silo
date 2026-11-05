#ifndef SILO_REQUEST_DRIVER_H
#define SILO_REQUEST_DRIVER_H

#include <stddef.h>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include "starvation_budget.h"

namespace queued {

using Ns = uint64_t;
constexpr Ns ms = 1000000;
constexpr Ns second = 1000 * ms;

enum class Kind { new_order, payment, synthetic_low, q2 };
// notification_checkpoint is a local C++ test backend, serviced only at audited
// reads. It is deliberately not exposed as the native Preemptive policy.
enum class Policy { wait, cooperative, notification_checkpoint };
enum class ServiceTransition { high_entry, high_empty, before_low_resume };

// Counters read after join; budget_snapshot() is the synchronized live API.
struct StarvationStats {
  uint64_t admission_blocks = 0;
  uint64_t checkpoint_blocks = 0;
  uint64_t forced_low_returns = 0;
  uint64_t low_requests = 0;
  uint64_t high_service_ns = 0;
  uint64_t low_service_ns = 0;
};

// Read only after producer and workers join. Notifications are hints; queue
// admission, attempts and terminal outcomes remain independent record counts.
struct NotificationStats {
  uint64_t publication_passes = 0;
  uint64_t admitted_high = 0;
  uint64_t deliveries = 0;
  uint64_t hints_consumed = 0;
  uint64_t queue_fallbacks = 0;
  uint64_t spurious_checks = 0;
  uint64_t deferrals = 0;
  uint64_t switches = 0;
  uint64_t high_entries = 0;
  uint64_t empty_checks = 0;
  uint64_t low_resumes = 0;
  bool pending_hint = false;
};
enum class Attempt { committed, conflict, user_aborted, failed };
enum class Outcome { pending, committed, user_aborted, retry_exhausted, failed,
                     rejected_queue_full, cancelled_shutdown };

struct Request {
  uint64_t id = 0; // Scoped to the run ID attached by the result writer.
  uint64_t batch = 0; // Zero denotes a low-priority request.
  Kind kind = Kind::synthetic_low;
  size_t worker = 0;
  size_t home_warehouse = 0;
  uint64_t input_seed = 0;
  uint64_t high_sequence = 0; // Run-wide, independent of low request generation.
  Ns scheduled = 0;
  Ns generated = 0;
  Ns admitted = 0;
  Ns first_start = 0;
  Ns finished = 0;
  unsigned attempts = 0;
  unsigned aborts = 0;
  Outcome outcome = Outcome::pending;
  uint64_t q2_checksum = 0;
  uint64_t cooperative_reads = 0;
  uint64_t cooperative_checks = 0;
  uint64_t cooperative_yields = 0;
  uint64_t low_service_ns = 0; // Context execution, excludes high-service pauses.
};

struct Config {
  size_t workers = 1;
  size_t batch_size = 4;
  Ns interval = ms;
  size_t global_capacity = 4;
  size_t high_capacity = 4;
  bool generate_low = true; // One pending low request per worker.
  Kind low_kind = Kind::synthetic_low;
  unsigned attempt_limit = 100;
  uint64_t seed = 1;
  Policy policy = Policy::wait;
  uint32_t yield_interval = 10000;
  unsigned starvation_threshold = 100; // Strict greater-than, monotonic ns locally.
};

Ns monotonic_now();

// One producer calls produce(); one consumer per worker calls run_worker().
// Each instance runs one phase. The coordinator joins all participants before
// visiting records, reporting, or destroying the driver.
class Driver {
public:
  using Execute = std::function<Attempt(const Request &)>;
  using Prepared = std::function<Attempt(uint64_t &checksum)>;
  using Prepare = std::function<Prepared(const Request &)>;
  using ReadCheckpoint = std::function<void()>;
  using Clock = std::function<Ns()>;
  using Wait = std::function<void(Ns)>;
  using Visit = std::function<void(const Request &)>;
  // Producer-side callback after high queue publication, once per worker per
  // nonempty admission pass. The callback must not wait for worker service.
  // An exception stops the run through the normal fatal-error drain path.
  using HighPublished = std::function<void(size_t)>;
  // Worker-side deterministic test seam, outside index/protection regions.
  using TransitionHook = std::function<void(size_t, ServiceTransition)>;

  explicit Driver(const Config &config, Clock clock = monotonic_now,
                  Wait wait_until = {}, HighPublished high_published = {},
                  TransitionHook transition = {});
  ~Driver();

  void produce(Ns duration, Ns drain, uint64_t first_id = 0, uint64_t first_high = 0);
  void run_worker(size_t worker, const Execute &execute);
  void run_prepared_worker(size_t worker, const Prepare &prepare);
  // Called on the low context after a completed, eligible record read.
  void on_record_read(size_t worker);
  void wait_for_drain();
  void notify(size_t worker);
  // Ordinary-thread modeled delivery; not an async-signal-safe handler API.
  void deliver_notification(size_t worker);
  NotificationStats notification_stats(size_t worker) const;
  StarvationBudget budget_snapshot(size_t worker) const;
  StarvationStats starvation_stats(size_t worker) const;
  size_t worker_count() const;
  unsigned budget_threshold() const;
  void visit_records(const Visit &visit) const;

  uint64_t generated() const;
  uint64_t high_generated() const;
  Ns cutoff() const;
  bool failed() const;

private:
  struct State;
  std::unique_ptr<State> state;
};

void write_header(std::ostream &out);
void write_results(const Driver &driver, std::ostream &out, std::ostream &summary,
                   const std::string &run_id, const char *phase);

} // namespace queued

#endif
