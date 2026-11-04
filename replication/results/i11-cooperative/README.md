# I11 Cooperative scheduling against Wait

November 4, 2026. Silo source base `834a3f8e4421cf605c2a8cbd1b99c83e829930d0` plus I11. Masstree `1150824e6a10a0384ba4bfed6b916ce62f96315f`.

## Policy and measurement

Select Cooperative with `--queued-policy cooperative --queued-mixed` on a Linux x86-64 glibc build configured with `WORKER_CONTEXTS=1`. The default interval is 10,000 completed supplier, stock, or item lookups per Q2 attempt. Override it with `--queued-yield-interval N`. The checkpoint runs after the index call returns. The worker pauses Q2 at an unprotected checkpoint when its high queue has work, serves admitted short requests in FIFO order on the original stack, then resumes the live snapshot on its alternate stack. Each stack uses separate transaction storage and an arena. Wait uses the same queue, retry, and timing definitions.

The [paired smoke configuration](../../experiments/configs/queued-cooperative-smoke.json) uses 64 reads so the short I09 Q2 profile reaches checkpoints. It runs both policies on the same binary at one and two workers, with one warehouse per worker, two seeds, one second each of warm-up and measurement, and up to 30 seconds of drain. Each 100 ms batch offers four short requests per worker. The low queue holds one pending demand-driven Q2 per worker. The Q2 input sequence matches for the shared prefix. The number of Q2s offered depends on when each policy frees queue slots.

The TSV fields `cooperative_reads`, `cooperative_checks`, and `cooperative_yields` describe mid-attempt scheduling. Switches into an idle alternate stack and returns after Q2 completion are outside those counts.

## Reproduce

Run from the Silo root. Initialize the pinned Masstree checkout and install the build dependencies first. The comparison used GCC 11.4 and glibc 2.35 in emulated Ubuntu x86-64, with the GCC pair formatter compatibility patch and `-O1` in a temporary build copy.

```sh
make -j1 -C third-party/lz4 library
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test
out=out-perf.masstree.contexts
./"$out"/replication/tests/tpcc_mixed_test
./"$out"/replication/tests/silo_context_test

result_dir=$(mktemp -d)
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread -DSILO_WORKER_CONTEXTS \
  benchmarks/request_driver.cc benchmarks/worker_context.cc replication/tests/request_driver_test.cc \
  -o "$result_dir/request_driver_test"
"$result_dir/request_driver_test"
python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v

python3 -B replication/experiments/run.py replication/experiments/configs/queued-cooperative-smoke.json \
  --source "$PWD" --dbtest "$PWD/$out/benchmarks/dbtest" --output "$result_dir" \
  --build-info 'GCC 11.4, MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1'
batch=$(find "$result_dir" -mindepth 1 -maxdepth 1 -type d)
python3 -B replication/experiments/analyze.py --output "$result_dir/analysis" batch "$batch"
python3 -B replication/tests/check_cooperative_batch.py "$batch"
```

## Results

All eight measured runs completed and reconciled with the phase summaries. They committed all 7,669 offered requests, including 432 short requests, with zero aborts, rejections, or cancellations. Cooperative recorded 38 mid-Q2 handoffs and served 152 short requests while Q2 was suspended. Wait recorded no overlap between a Q2 and a short request on the same worker. The trace check matched the high-priority inputs and relative arrival deadlines across each pair, as well as the shared Q2 input prefixes. [Trace reconciliation](reconciliation.log)

Each table cell shows Wait → Cooperative. Latency is the p99 from generation to commit, in milliseconds. Throughput counts commits within the one-second window.

| Seed | Workers | New-Order p99 (ms) | Payment p99 (ms) | Q2 p99 (ms) | Throughput (txn/s) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 1.108 → 0.964 | 1.024 → 0.604 | 3.027 → 4.703 | 785 → 733 |
| 1 | 2 | 4.007 → 0.628 | 4.096 → 0.437 | 7.861 → 8.623 | 1,210 → 1,133 |
| 2 | 1 | 1.694 → 0.699 | 1.625 → 0.760 | 4.569 → 3.858 | 729 → 735 |
| 2 | 2 | 2.134 → 1.697 | 3.622 → 1.800 | 7.984 → 8.265 | 1,204 → 1,126 |

New-Order and Payment p99 fell in all four pairs. Q2 p99 rose in three pairs, and overall throughput fell in three. The short-request samples contain 16–37 observations per transaction type and run, so their p99s are sample maxima. These are preliminary measurements on an emulated two-CPU Linux guest with one-second windows.

- The [deterministic driver suite](request-driver.log) passed in optimized and UndefinedBehaviorSanitizer builds. It verified an empty interval, deferred service through nested protection, FIFO and repeated handoffs, high aborts, and a low retry with the original input.
- The [TPC-C engine harness](tpcc-mixed-test.log) kept the expected Q2 checksum across 18 eligible reads and nine pauses while the original stack committed writes. The [I08 engine harness](silo-context-test.log) also passed snapshot isolation, abort cleanup, and reclamation checks.
- The [command log](validation.log) records the rebuilt binary's engine tests, paired experiment, CLI rejections, stock smoke, and separate default-interval smoke. The 709 Q2s in that smoke finished below 10,000 reads, with zero checks and zero yields. [Default phase summary](default-interval.err) · [Default request trace](default-interval.tsv.gz) · [Environment](environment.log) · [Runner and analyzer tests](experiment-tests.log)
- The [batch archive](batch.tar.gz) contains the raw TSVs and phase logs, per-run manifests, and exact Silo and Masstree diffs. Extract it into a temporary directory and pass its `batch/` directory to the analysis and trace-check commands above. The [batch metadata](batch.json) and [per-run latency and throughput CSV](queued-summary.csv) are also available separately.
