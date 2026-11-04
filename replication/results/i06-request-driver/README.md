# I06 request driver validation

November 3, 2026. Source base `05d1c2e4b1050465353efd68d1abc6c57d649b17` plus the I06 working-tree changes. The driver, benchmark bridge, and test are in [request_driver.cc](../../../benchmarks/request_driver.cc), [bench.cc](../../../benchmarks/bench.cc), and [request_driver_test.cc](../../tests/request_driver_test.cc).

## Reproduce the standalone check

From the Silo checkout root with a C++11 compiler and POSIX threads:

```sh
test_dir=$(mktemp -d)
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread \
  benchmarks/request_driver.cc replication/tests/request_driver_test.cc \
  -o "$test_dir/request_driver_test"
"$test_dir/request_driver_test"
```

[Standalone output](standalone.log) includes the ordinary run, 25 additional executions, and an AddressSanitizer and UndefinedBehaviorSanitizer run. The suite also passed under [ThreadSanitizer](thread-sanitizer.log) and [Linux GCC](linux-standalone.log). The simulated clock makes the missed-deadline, queue-full, cancellation and retry cases reproducible. One fixture produces 10 IDs, of which 5 are rejected, 4 are cancelled and 1 commits after the drain deadline. It records a failed phase because an active attempt crossed that deadline. The real-clock fixture holds a synthetic low-priority attempt while the producer admits a short request. Its output separates global queue time, local queue time, execution and end-to-end latency. Both fixtures are functional evidence.

## Opt-in TPC-C smoke profile

Build `dbtest` for Linux x86-64 with the [I04 build setup](../../pace-baseline/README.md). Run the queued profile with one warehouse per worker:

```sh
./out-perf.masstree/benchmarks/dbtest --bench tpcc --num-threads 1 \
  --scale-factor 1 --runtime 1 --queued i06-w1 --queued-warmup 1 \
  --queued-interval-us 100000 > queued-w1.tsv 2> queued-w1.err
```

The opt-in uses time-based TPC-C on `ndb-proto2` with logging disabled. It runs a seeded 50/50 New-Order and Payment distribution and uses stock TPC-C's transaction bodies. The input recipe freezes type, home warehouse, seed and a logical date derived from the request ID. A fresh engine transaction reconstructs the same inputs on retry. Conflicts retry immediately with a limit of 100 total attempts. The standalone suite uses synthetic low-priority work. Real Q2 and full input materialization are I09. The stock command without `--queued` uses its original dispatch loop and five-column result.

| Flag | Default |
|---|---|
| `--queued RUN_ID` | Opt-in. Supply a unique identifier for each run |
| `--queued-batch-size N` | Four arrivals per worker per batch |
| `--queued-interval-us N` | 1000 µs |
| `--queued-global-capacity N` | Four pending requests per worker in the global FIFO |
| `--queued-worker-capacity N` | Four pending high-priority requests per worker |
| `--queued-seed N` | 1. Warm-up uses a separate derived seed |
| `--queued-warmup SECONDS` | 5. Zero disables warm-up |
| `--queued-drain SECONDS` | 30 |

Warm-up finishes draining before the measured readiness barrier. Phase windows are half-open. Batch deadlines strictly before cutoff belong to the phase even when the producer wakes late. The producer attempts bounded FIFO admission and targets a capacity poll every 100 µs or at the next deadline. At the drain deadline, pending requests are cancelled and active calls return at a safe attempt boundary. An active call that crosses the deadline fails the phase. The synchronous driver waits for the adapter to return, so an adapter stuck inside a transaction requires external intervention.

Standard output holds one TSV row per generated ID, including rejected and cancelled requests. Rows are grouped by recorder after join. Sort by `request_id` to recover generation order. Missing timestamps are `null`. Standard error reports terminal counts, cutoff states, commits before cutoff and drain commits per phase. `cutoff_due_ungenerated` counts already scheduled arrivals recorded after cutoff and is separate from the requests that were outstanding at cutoff. To derive times in nanoseconds, subtract `scheduled_ns` from `generated_ns` for generation delay, `generated_ns` from `admitted_ns` for global queue time, `admitted_ns` from `first_start_ns` for local queue time, and `generated_ns` from `finished_ns` for end-to-end latency. Execution elapsed runs from `first_start_ns` to `finished_ns` and includes retries. The result writer keeps all records in memory during each phase. I05 still owns experiment manifests and aggregate analysis.

## Linux smoke evidence

The [build and verification log](linux-smoke.log) records an emulated Linux x86-64 build with GCC 11.4.0 and pinned Masstree. The temporary build copy used the I04 pair-formatter and Berkeley DB compatibility changes, with `-O1` and warnings left enabled. All smoke runs exited successfully. These results validate the dispatch path and record format.

- Stock TPC-C produced five numeric result columns. [Stock output](stock.out)
- One worker committed all 36 warm-up and 36 measured requests. The last warm-up completion preceded the start of measurement. [Request rows](queued-w1.tsv) [Phase summaries](queued-w1.err)
- Two workers committed all 72 measured requests. The verifier checked unique IDs, both transaction types, worker and warehouse routing, batch deadlines, timestamp order and reconciliation. [Request rows](queued-w2.tsv) [Phase summary](queued-w2.err)
