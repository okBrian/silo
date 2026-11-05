#ifndef SILO_REQUEST_DRIVER_H
#define SILO_REQUEST_DRIVER_H

#include <stddef.h>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace queued {

using Ns = uint64_t;
constexpr Ns ms = 1000000;
constexpr Ns second = 1000 * ms;

enum class Kind { new_order, payment, synthetic_low, q2 };
enum class Policy { wait, cooperative };
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

  explicit Driver(const Config &config, Clock clock = monotonic_now,
                  Wait wait_until = {}, HighPublished high_published = {});
  ~Driver();

  void produce(Ns duration, Ns drain, uint64_t first_id = 0, uint64_t first_high = 0);
  void run_worker(size_t worker, const Execute &execute);
  void run_prepared_worker(size_t worker, const Prepare &prepare);
  // Called on the low context after a completed, eligible record read.
  void on_record_read(size_t worker);
  void wait_for_drain();
  void notify(size_t worker);
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
