# I05 experiment runner and analysis

The runner accepts a named JSON configuration and an already built `dbtest`. Use Python 3.8 or newer for the runner, analyzer, and tests. SVG plotting additionally uses ReportLab (`reportlab==4.4.9` was used for the saved plots). Run the examples from the Silo checkout root.

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

Each batch has a unique directory and a `batch.json` containing the full configuration, host and Slurm job, Silo and initialized Masstree revisions, tracked diffs and hashes, binary hash, and build information. Use a committed source tree or retain the recorded patches and any untracked build inputs. Each planned invocation has its own directory and manifest with the exact argument vector, seeds, phase durations and exit status. Stdout and stderr are kept even when an invocation fails. The wall timeout includes loading and both possible queued drain windows. The runner returns nonzero if any invocation fails. A terminated batch retains its last saved state for each invocation, including planned invocations.

Variants inherit the allocation's CPU mask by default. Set an optional `cpus` list on a variant to restrict its process and threads to an allocated subset using Linux `taskset`. The current benchmark modes leave individual workers and the producer unpinned, so their fixed CPU IDs are null. The stock runtime reported by verbose `dbtest` is captured separately from the configured measurement and the runner's total wall time.

Use the experiment directory printed by the runner to analyze the batch.

```sh
python3 replication/experiments/analyze.py --output "$output_dir/batch" batch "$experiment_dir"
python3 replication/experiments/plot.py "$output_dir/batch/analysis.json" --output "$output_dir/batch"
```

## Read the tables

- **Stock.** The five fields are committed throughput, reported persistence throughput, average latency of timed successful attempts in milliseconds, reported persistence latency in milliseconds, and aborts per second. The table converts attempt latency to microseconds and averages the available run means with equal weight. The CSV includes counts of contributing runs. The persistence columns are raw stock fields for the logging-disabled configuration. Individual attempt samples are unavailable.
- **Queued.** Total queue delay is first start minus generation. The CSV and JSON also separate global queue time (admission minus generation) and local queue time (first start minus admission). End-to-end latency is finish minus generation. Scheduled latency is finish minus scheduled arrival. Each distribution uses committed requests for the phase and transaction type, including commits during drain. Window throughput uses only commits strictly before the cutoff, divided by the configured measurement duration. Drain completions count terminal results at or after cutoff that were neither rejected nor cancelled. Drain commits are the committed subset.
- **Tails and failures.** Percentiles use nearest rank. Empty distributions have zero samples and null percentiles. Each p99.9 estimate with 1–999 samples is flagged. Run-level CSVs and JSON keep failed and incomplete runs. CSV metrics without evidence are blank. With a missing I06 phase summary, valid terminal rows are reported as observed and cutoff-dependent metrics are null. A summary that disagrees with its TSV is retained for diagnosis and excluded from cohort statistics. Stock aggregates and plots use completed runs. Queued plots use complete or phase-verified measurement cohorts and display p50 and p99.
