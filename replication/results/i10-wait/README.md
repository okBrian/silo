# I10 Wait scheduler

November 4, 2026. Silo source base `1d763ee97838d1593ef80b30e1c48ec3572a4910` plus I10. Masstree `1150824e6a10a0384ba4bfed6b916ce62f96315f`.

## Policy

`--queued-policy wait` selects Wait for a queued run. Omitting the option selects the same policy. Each worker runs one request through its terminal outcome, including immediate conflict retries with the same prepared input. At the next dispatch boundary the worker takes the oldest admitted high-priority request before taking another low-priority request. An arriving short request can queue while a Q2 is active. Dispatch uses the worker's original stack and the existing queue wakeup.

The [driver test](../../tests/request_driver_test.cc) blocks the first synthetic long request, admits two shorts and a second long request, then releases the running request. The short requests start in FIFO order after 7 and 8 µs of local queueing. The second long request starts after both shorts finish.

## Reproduce

Run the scheduling and runner tests from the Silo root.

```sh
test_dir=$(mktemp -d)
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread \
  benchmarks/request_driver.cc replication/tests/request_driver_test.cc \
  -o "$test_dir/request_driver_test"
"$test_dir/request_driver_test"
python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v
```

Build on Linux x86-64 with the pinned Masstree source and the Silo build dependencies. The emulated Ubuntu build used GCC 11.4, glibc 2.35 and the temporary I08 compatibility changes for the GCC pair formatter and Berkeley DB, with `-O1` and warnings enabled. The batch includes the exact source diff.

```sh
make -j1 -C third-party/lz4 library
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 tpcc_mixed_test dbtest
out=out-perf.masstree.contexts
./"$out"/replication/tests/tpcc_mixed_test

result_dir=$(mktemp -d)
python3 -B replication/experiments/run.py replication/experiments/configs/queued-wait-smoke.json \
  --source "$PWD" --dbtest "$PWD/$out/benchmarks/dbtest" --output "$result_dir" \
  --build-info 'GCC 11.4, MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1'
batch=$(find "$result_dir" -mindepth 1 -maxdepth 1 -type d)
python3 -B replication/experiments/analyze.py --output "$result_dir/analysis" batch "$batch"
```

The [Wait smoke configuration](../../experiments/configs/queued-wait-smoke.json) uses the I09 mixed profile with one warehouse per worker, one second of warm-up, one second of measurement, and up to 30 seconds of drain per phase. Each 100 ms batch offers four high requests per worker with equal selection probabilities for New-Order and Payment. The low queue holds one pending, demand-driven Q2 per worker. The runner executes one- and two-worker variants twice with measurement seeds 1 and 2 and reverses variant order on the second repetition.

## Results

All four runs finished successfully. The measurement cohorts committed all 4,236 offered requests with zero aborts, rejections or cancellations. Counts by transaction type include drain commits.

| Repetition | Workers | New-Order | Payment | Q2 | Commits in window | Drain commits |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 20 | 16 | 737 | 772 | 1 |
| 1 | 2 | 36 | 36 | 1,285 | 1,355 | 2 |
| 2 | 2 | 37 | 35 | 1,250 | 1,319 | 3 |
| 2 | 1 | 20 | 16 | 748 | 782 | 2 |

The analyzer reconciled the terminal rows with all eight phase summaries. A separate trace check verified the high arrival deadlines, round-robin routing, and serial execution on each worker. During measurement, 120 short requests were admitted while a Q2 was running on their assigned worker. In repetition 1 with one worker, request 934 spent 1.621 ms in the local queue. The active Q2 finished 1.617 ms after request 934 was admitted. [Trace reconciliation](reconciliation.log)

- [Batch configuration and provenance](batch/batch.json). Each run directory holds its manifest, raw request TSV in `stdout.log`, and phase summaries in `stderr.log`. [Exact build source diff](batch/silo.diff)
- [I05 summary with latency percentiles](queued-summary.md) and [CSV with queue and end-to-end distributions](queued-summary.csv).
- [Linux environment](environment.log), [build and experiment command log](validation.log), [TPC-C engine harness](tpcc-mixed-test.log), and [CLI rejection checks](cli-checks.log).
- [Standalone driver suite](request-driver.log), [address and undefined-behavior sanitizer run](request-driver-sanitized.log), and [ten experiment-tool tests](experiment-tests.log). The standalone suite also passed 25 consecutive optimized runs.

These one-second measurements provide a local baseline on emulated Linux x86-64. Native PACE validation and comparisons with Cooperative and Preemptive remain open.
