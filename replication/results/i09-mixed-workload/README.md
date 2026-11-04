# I09 mixed workload

November 4, 2026. Silo source base `642e51553f870f56ec6f22126874a10c273a12d6`, Masstree `1150824e6a10a0384ba4bfed6b916ce62f96315f`. The [I02 source map](../../docs/source-map.md#q2-behavior-and-port-decision) identifies the reference revision and the Q2 fixes.

## Workload and data

- `--queued RUN_ID --queued-mixed` runs New-Order and Payment as high-priority work with equal selection probabilities, and demand-driven Q2 as low-priority work with one pending request per worker. Workers use their fixed home warehouse for short requests. Q2 joins stock across all warehouses. This profile uses the I06 serial dispatch loop. The Wait and Cooperative comparisons belong to I10 and I11.
- [The TPC-C adapter](../../../benchmarks/tpcc.cc) prepares each queued payload once on its worker before opening a transaction. New-Order freezes the home and district, customer, and each item's ID, supplier warehouse and quantity. Payment freezes both warehouse/district pairs, amount and customer lookup mode and key. The by-name median remains a transactional lookup. Q2 freezes its uniformly chosen region. A retry opens a fresh transaction with the same payload. The short transactions share the high half of the 32-bit logical date range, assigned by the offered high-request sequence across both phases. Low arrivals and high-request rejection cannot shift another high request's date. Queued Payment writes history with the customer ID, district and warehouse in the order declared by the schema and used by the loader.
- The adapter uses Silo's item and customer distributions, 1% remote supplier selection per New-Order item and 15% remote Payment customer selection, with remote selection disabled at one warehouse. Queued New-Order allocates order IDs transactionally. As in stock Silo, the input generator uses valid item IDs and omits the TPC-C deliberate invalid-item rollback case. Engine conflicts use the driver's 100-attempt limit.
- [Q2](../../../benchmarks/tpcc_q2.h) uses a Silo protocol-2 read-only snapshot. It scans five regions and 62 nations, checks supplier IDs 0–99 for each nation in the target region, and looks up stock with the reference mapping `(warehouse * item) % 10000`. The port stores just the dimension keys and join columns. It retains the audited region/nation assignments and uses loader seed 51271928 to assign the 10,000 suppliers uniformly over the same 62 nation keys. Projecting out supplier addresses changes the RNG stream from the reference loader. Stock and item values come from the Silo loaders and tables shared with the short transactions.
- `q2_min_stock_v1` retains the first minimum in warehouse/item order, skips suppliers with empty stock lists and filters the selected item when its data contains lowercase `b`. It hashes each qualifying tuple as six little-endian 32-bit fields in scan order using 64-bit FNV-1a. The fields are region, nation, supplier, stock warehouse, item and stock quantity. The terminal TSV includes `q2_checksum` for committed Q2 and `null` otherwise. The empty result hashes to the FNV offset basis.

## Reproduce

Build on Linux x86-64 with the pinned Masstree source. Use the build-copy compatibility guidance from [I08](../i08-silo-context/README.md#reproduce) if needed.

```sh
make -j1 -C third-party/lz4 library
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h
make -j3 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 tpcc_mixed_test dbtest
out=out-perf.masstree.contexts
./"$out"/replication/tests/tpcc_mixed_test
python3 replication/experiments/run.py replication/experiments/configs/queued-mixed-smoke.json \
  --source "$PWD" --dbtest "$PWD/$out/benchmarks/dbtest" --output "$PWD/results" \
  --build-info 'GCC 11.4; MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1'
```

The equivalent direct benchmark invocation for two workers is below. The named config also runs a one-worker variant.

```sh
./"$out"/benchmarks/dbtest --bench tpcc --num-threads 2 --scale-factor 2 \
  --runtime 1 --queued i09-mixed-w2 --queued-mixed --queued-warmup 1 \
  --queued-interval-us 100000 --verbose > mixed-w2.tsv 2> mixed-w2.err
```

## Local validation

Built and ran on emulated Linux x86-64 with GCC 11.4.0, glibc 2.35 and worker-context instrumentation enabled. The temporary build copy excluded Berkeley DB, declared the existing pair formatter before the calling template, and used `-O1` with warnings enabled. The [command log](validation.log) records the build, the engine harness, argument checks and the direct `dbtest` runs.

The [engine harness](../../tests/tpcc_mixed_test.cc) loaded two tiny warehouses. Its independently calculated Q2 checksums covered unequal quantities, a tie resolved by mapping order, a supplier without mapped stock, an unmatched supplier nation, a different region, a region without a qualifying row, and lowercase versus uppercase item data. It also forced commit failures and verified that retries reused the prepared input. For New-Order it checked rollback, district order ID, linked order records and remote stock changes. For Payment it checked rollback, lower-median lookup for a remote customer, balances, payment count and the history row. [Engine test output](tpcc-mixed-test.log)

All offered requests committed in both mixed runs. The [reconciliation](reconciliation.log) checked unique IDs, round-robin high routing, phase counts, priority and checksum presence. The I05 analyzer separately reconciled lifecycle timestamps and cutoff counts for all three queued runs.

| Run | Phase | New-Order | Payment | Q2 | Drain commits |
| --- | --- | ---: | ---: | ---: | ---: |
| Short only, 1 worker | Measurement | 20 | 16 | — | 0 |
| Mixed, 1 worker | Warm-up | 24 | 12 | 735 | 1 |
| Mixed, 1 worker | Measurement | 20 | 16 | 747 | 2 |
| Mixed, 2 workers | Warm-up | 43 | 29 | 1,250 | 2 |
| Mixed, 2 workers | Measurement | 36 | 36 | 1,303 | 3 |

The stock smoke produced its [five-column summary](stock.out). The [short-only records](short-w1.tsv), [one-worker mixed records](mixed-w1.tsv) and [two-worker mixed records](mixed-w2.tsv) each have adjacent `.err` phase summaries. The [standalone driver test](request-driver.log) and [nine experiment-tool tests](experiment-tests.log) also passed.

```sh
e=replication/results/i09-mixed-workload
python3 -B replication/experiments/analyze.py --output "$(mktemp -d)" \
  queued --measurement-s 1 "$e"/{short-w1,mixed-w1,mixed-w2}.tsv
python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread \
  benchmarks/request_driver.cc replication/tests/request_driver_test.cc -o /tmp/i09-request-driver
/tmp/i09-request-driver
```

These one-second runs validate dispatch and accounting. Native PACE validation, policy comparisons and final performance measurements remain open. I10 can use the mixed profile for the Wait baseline.
