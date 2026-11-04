# PACE stock Silo TPC-C baseline

The ten measurement rows come from the completed benchmark phase of PACE job `6791465` on November 3, 2026. The CSV was verified against the job archive. The batch configuration specifies Silo `cc11ca1`, Masstree `1150824`, the `ndb-proto2` backend, two warehouses, and 30 seconds per run. The corrected script applies the Berkeley DB exclusion and GCC formatter compatibility changes to the temporary build.

| Workers | Runs | Throughput mean ± sample SD (txn/s) | Throughput range (txn/s) | Mean attempt latency (µs) | Mean aborts/s |
|---:|---:|---:|---:|---:|---:|
| 1 | 5 | 84,254 ± 8,538 | 70,944–94,629 | 11.94 | 0.00 |
| 2 | 5 | 148,330 ± 13,622 | 136,536–168,677 | 13.53 | 6.30 |

At fixed scale, two workers averaged **1.76×** the committed throughput of one worker (**76.1%** higher). Two-worker runs also had a higher mean of the per-run successful-attempt latency averages and recorded some aborts. Each latency average in the table gives equal weight to each run.

These stock TPC-C measurements establish a performance baseline. The queued mixed-workload driver will supply scheduling delay and per-request end-to-end latency. Slurm marked job `6791465` failed (`134:0`, elapsed `00:06:23`) after all ten benchmark rows were produced. The upstream test aborted at `rcu.cc:292` because `numa_run_on_node(node)` failed during test thread pinning.

The test-only rerun, job `6791730`, selected CPUs from the inherited Slurm affinity mask and retained the original NUMA binding call and invariant checks. Slurm recorded `COMPLETED` with exit `0:0` in `00:05:26`. The diagnostic transaction abort counts in its test output are separate from the process exit status and the benchmark's abort-rate measurements. The [benchmark logs](../pace-runs/6791465/) and [test rerun logs](../pace-runs/6791730/) contain the environment and exit-status records for both jobs.
