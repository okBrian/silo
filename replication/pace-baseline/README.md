# I04 on PACE

This job builds Silo from the supplied checkout's pinned revision, runs the stock `dbtest` benchmark, and runs the upstream debug suite with invariant checks. It uses the Masstree index, Silo `ndb-proto2`, and the stock TPC-C transaction mix. Build settings use libc allocation and omit the MySQL and Berkeley DB backends. The job builds the bundled LZ4 library serially to avoid a recursive Make jobserver error. It removes `-Werror` from the temporary Makefile, keeps warnings and optimizations enabled, and records the temporary build and adapter changes in `build-compatibility.diff`.

## Prepare the source on PACE

1. Open this Silo checkout on a PACE login node. From the repo root, record its path and stage the pinned engine under your home directory. Run this once in a shell with GitHub access.

   ```bash
   repo_root=$(git rev-parse --show-toplevel)
   run_dir=$(mktemp -d "$HOME/silo-i04.XXXXXX")
   cd "$run_dir"

   git clone --no-local --no-checkout "$repo_root" silo-input
   git -C silo-input checkout --detach cc11ca1ea949ef266ee12a9b1c310392519d9e3b
   git -C silo-input config submodule.masstree.url https://github.com/kohler/masstree-beta.git
   git -C silo-input submodule update --init --checkout masstree
   cp "$repo_root/replication/pace-baseline/stock-silo.sbatch" .
   git -C silo-input submodule status
   ```

2. Verify the Masstree revision is `1150824e6a10a0384ba4bfed6b916ce62f96315f`. A leading minus sign indicates an uninitialized submodule. If you already staged the two pinned repositories from the original PACE package, use that directory and copy in the current batch script. The job clones from the staged local source on the compute node.

3. Load the compiler or library modules available to your account before submitting if needed. The build expects GCC/G++, GNU Make, Autoconf tooling, and NUMA development files. The temporary build excludes the Berkeley DB adapter and drops unused libraries from the benchmark link command, addressing the missing header in PACE job 6791273. The patch also declares the existing pair formatter before the `format_list` template, addressing the GCC 12.3 lookup failure in job 6791405.

## Submit the job

From the staging directory on the login node, use the settings that worked for this course:

```bash
sbatch -A coc -p coc-cpu --qos=coc-ice stock-silo.sbatch "$PWD/silo-input"
```

Add the account, partition, or QoS flags your course allocation requires before the script name. The job requests one CPU node with four CPUs, 16 GiB of memory, and one hour. It builds in temporary storage, captures results in the submission directory, and reuses the staged Masstree checkout for compute-node execution. The memory request leaves room for the upstream stress suite. A longer local correctness attempt was killed under memory pressure in an approximately 8 GiB Docker VM while the second validation job was running.

By default, the job runs one five-second smoke run followed by five 30-second repetitions each for one and two workers at a fixed scale of two warehouses. Run order alternates across repetitions. For an initial diagnostic job, set `SILO_REPEATS=1 SILO_RUNTIME=5` in the submission environment. Every invocation reloads the database. The stock executable's timed interval starts after loading and includes its early execution period. The five-second smoke run is separate and excluded from the CSV.

The benchmark uses the allocation's affinity and omits Silo's explicit `--pin-cpus` and `--numa-memory` settings. The upstream correctness suite runs after the benchmark, with a ten-minute time limit (`SILO_TEST_TIMEOUT=600` seconds). Job 6791465 completed its benchmarks and aborted in the test suite when `numa_run_on_node` failed. The updated job selects CPUs from the process's allowed affinity mask for the pinning test and logs the selections. The original NUMA call and invariant checks still run. To rerun only the adapted test suite on staged sources, use `sbatch --export=ALL,SILO_TEST_ONLY=1 stock-silo.sbatch "$PWD/silo-input"`. Record the outcome separately from the earlier benchmark job.

## Check the evidence

- Open `silo-stock-<job-id>.log` to find the results directory and first failing command, if any.
- In `results/silo-stock-<job-id>.*`, `exit-status` records the job script result, `environment.txt` records revisions and compute settings, and `test.log` records upstream test output. `test-passed.txt` appears after a successful test exit.
- Inspect `stock-tpcc.csv` and the per-run `.stdout` and `.stderr` logs. A complete default run has ten measurement rows and a zero exit status. Preserve partial rows and the failure logs if a run stops early.
- The CSV keeps the five metrics from stock `dbtest`. Use throughput, the reported average successful-attempt latency, and abort rate for the milestone. The persistence fields are raw stock fields, and the run passes no durability log. Queuing delay and end-to-end percentiles will come from I06 and later integration.

## Submit the I05 stock configuration

Use the same staged stock source and build job. Supply the runner and configuration as two additional arguments. From the Silo checkout root on the login node:

```bash
repo_root=$(git rev-parse --show-toplevel)
sbatch -A coc -p coc-cpu --qos=coc-ice \
  "$repo_root/replication/pace-baseline/stock-silo.sbatch" "$source_dir" \
  "$repo_root/replication/experiments/run.py" \
  "$repo_root/replication/experiments/configs/stock-tpcc-2wh.json"
```

Set `source_dir` to the staged `silo-input` path from the setup above. This mode uses the JSON settings for the benchmark phase and writes `experiments/<batch-id>/` inside the job's results directory. The original one-argument workflow still accepts the `SILO_REPEATS` and `SILO_RUNTIME` environment overrides. The upstream suite runs after the I05 benchmark matrix. Use the [I05 analyzer](../experiments/README.md#read-the-tables) on the inner batch directory.
