# I05 experiment runner and analysis

The runner accepts a named JSON configuration and an already built `dbtest`. Use Python 3.8 or newer for the runner, analyzer, and tests. SVG plotting additionally uses ReportLab (`reportlab==4.4.9` was used for the saved plots). Run the examples from the Silo checkout root.

For the MAC-05 clean-build and paired smoke commands, archive replay, and the planned
native three-policy collection, use [the host workflow](HOST_WORKFLOW.md). The
[Figure 10 target](plans/figure10.json) is a planning artifact: Preemptive execution,
individual worker/sender pinning and the final policy-order schedule are pending.
The [MAC-05 evidence](../results/mac05-workflow/README.md) records what actually ran.

## Reproduce the archived results

These commands read the existing PACE and I06 archives.

```sh
output_dir=$(mktemp -d)
python3 replication/experiments/analyze.py --output "$output_dir/pace" \
  pace replication/results/pace-runs/6791465
python3 replication/experiments/analyze.py --output "$output_dir/queued" \
  queued --measurement-s 1 replication/results/i06-request-driver/queued-w{1,2}.tsv
python3 replication/experiments/plot.py "$output_dir/pace/analysis.json" \
  --output "$output_dir/pace"
python3 replication/experiments/plot.py "$output_dir/queued/analysis.json" \
  --output "$output_dir/queued"
python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v
```

The [regenerated tables and plots](../results/i05-experiments/README.md) use those inputs. The PACE import reads the five stdout columns directly. The legacy Bash trace shows whether each benchmark returned successfully. Job 6791465's exit 134 belongs to the upstream test failure after the benchmark loop. The legacy archive provides a job exit code and successful benchmark traces, so imported per-run exit codes are null. For saved I06 TSVs, adjacent `.err` files supply the phase cutoffs, counters and phase failure flag. `phase-verified` indicates successful reconciliation with those phase summaries. Use `--measurement-s` only when the duration is known.

## Run a new batch on Linux

Use [the stock PACE build job](../pace-baseline/README.md#submit-the-i05-stock-configuration) for the pinned stock engine. With a prebuilt binary from the supplied source checkout, run directly within the allocation.

```sh
python3 replication/experiments/run.py replication/experiments/configs/queued-tpcc-smoke.json \
  --source "$PWD" --dbtest "$PWD/out-perf.masstree/benchmarks/dbtest" \
  --output "$PWD/results" --build-info 'GCC 11.4; MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0'
```

Use the build information from your actual binary. The queued configuration requires an I06-capable binary. The named stock configuration fixes two warehouses, one and two workers, and five 30-second repetitions. Each command loads a fresh dataset. The runner reverses the variant order on even repetitions. The queued profile uses one warehouse per worker and seeds `first_seed + repetition - 1`, shared by variants in a repetition. It records the derived warm-up seed and fixed TPC-C loader seeds. Stock uses its fixed worker seed generator.

Use [queued-mixed-smoke.json](configs/queued-mixed-smoke.json) with an I09-capable binary to add demand-driven Q2 to the queued New-Order and Payment traffic. The config selects `q2_min_stock_v1` and records its dataset, supplier loader seed and input distributions in the manifest. Committed Q2 rows carry a deterministic result checksum, which the analyzer validates as an unsigned 64-bit value. [I09 workload and reproduction commands](../results/i09-mixed-workload/README.md)

Use [queued-wait-smoke.json](configs/queued-wait-smoke.json) for the I10 baseline. The queued config's `"policy": "wait"` passes `--queued-policy wait` and records the policy in each manifest. Omit the field to use the default queued dispatch. Wait runs each request through completion and gives admitted short requests priority at the next dispatch boundary. [I10 commands and results](../results/i10-wait/README.md)

Use [queued-cooperative-smoke.json](configs/queued-cooperative-smoke.json) with a `WORKER_CONTEXTS=1` binary for a paired I11 batch. Queued variants can override the top-level `policy`. A top-level `yield_interval` applies to Cooperative variants and defaults to 10,000 reads when omitted. Manifests record the effective interval for each Cooperative run. The smoke uses 64 reads and shares input seeds, queue settings, and phase lengths across each policy pair. [I11 commands and results](../results/i11-cooperative/README.md)

Each batch has a unique directory and a `batch.json` containing the full configuration, host and Slurm job, Silo and initialized Masstree revisions, tracked diffs and hashes, binary hash, and build information. Use a committed source tree or retain the recorded patches and any untracked build inputs. Each planned invocation has its own directory and manifest with the exact argument vector, seeds, phase durations and exit status. Stdout and stderr are kept even when an invocation fails. The wall timeout includes loading and both possible queued drain windows. The runner returns nonzero if any invocation fails. A terminated batch retains its last saved state for each invocation, including planned invocations.

Variants inherit the allocation's CPU mask by default. Set an optional `cpus` list on a variant to restrict its process and threads to an allocated subset using Linux `taskset`. The current benchmark modes leave individual workers and the producer unpinned, so their fixed CPU IDs are null. The stock runtime reported by verbose `dbtest` is captured separately from the configured measurement and the runner's total wall time.

Use the experiment directory printed by the runner to analyze the batch.

```sh
python3 replication/experiments/analyze.py --output "$output_dir/batch" batch "$experiment_dir"
python3 replication/experiments/plot.py "$output_dir/batch/analysis.json" --output "$output_dir/batch"
```

For queued batches, `report.py` adds `per-type.csv`, `run-health.csv`, and `report.md`.

```sh
python3 replication/experiments/report.py "$output_dir/batch/analysis.json" \
  --output "$output_dir/batch" --label 'Describe the actual host and smoke/performance scope' \
  --require-complete
```

It preserves per-repetition metrics with policy/worker labels, all latency quantiles
and sample counts, outcome/abort counts, outstanding work at cutoff and unaccounted
work at exit. Unknown exit accounting stays blank/unknown. `--require-complete`
returns nonzero after writing the report if any run failed or lacks reconciliation;
the analyzer alone retains failures without enforcing a successful-batch gate.

## Read the tables

- **Stock.** The five fields are committed throughput, reported persistence throughput, average latency of timed successful attempts in milliseconds, reported persistence latency in milliseconds, and aborts per second. The table converts attempt latency to microseconds and averages the available run means with equal weight. The CSV includes counts of contributing runs. The persistence columns are raw stock fields for the logging-disabled configuration. Individual attempt samples are unavailable.
- **Queued.** Total queue delay is first start minus generation. The CSV and JSON also separate global queue time (admission minus generation) and local queue time (first start minus admission). End-to-end latency is finish minus generation. Scheduled latency is finish minus scheduled arrival. Each distribution uses committed requests for the phase and transaction type, including commits during drain. Window throughput uses only commits strictly before the cutoff, divided by the configured measurement duration. Drain completions count terminal results at or after cutoff that were neither rejected nor cancelled. Drain commits are the committed subset.
- **Tails and failures.** Percentiles use nearest rank. Empty distributions have zero samples and null percentiles. Each p99.9 estimate with 1–999 samples is flagged. Run-level CSVs and JSON keep failed and incomplete runs. CSV metrics without evidence are blank. With a missing I06 phase summary, valid terminal rows are reported as observed and cutoff-dependent metrics are null. A summary that disagrees with its TSV is retained for diagnosis and excluded from cohort statistics. Stock aggregates and plots use completed runs. Queued plots use complete or phase-verified measurement cohorts and display p50 and p99.
