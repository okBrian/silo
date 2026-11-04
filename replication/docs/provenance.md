# Source and evidence provenance

- Silo baseline commit is `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`. The Masstree gitlink pins `1150824e6a10a0384ba4bfed6b916ce62f96315f`.
- The reference project and paper are linked in the [implementation roadmap](../IMPLEMENTATION_PLAN.md). Its scheduler integration remains open.
- [PACE job 6791465](../results/pace-runs/6791465/) produced all ten stock TPC-C measurements before the invariant suite aborted during NUMA pinning. [PACE job 6791730](../results/pace-runs/6791730/) completed the adapted invariant suite with exit zero. Both environments record AMD EPYC 7513 and Spack GCC 12.3.0.
- The PACE job records include all 42 log and output files from the downloaded archive. User login and home-directory strings are replaced with placeholders. The measurements, exit statuses, and compatibility diffs are preserved. The original compressed archive's SHA-256 is `883642f9b3c1d94790abf69a23dc9eb9bf80b8c91e54875c163bb097e9546ba5`.
- The [local smoke files](../results/i04-local-validation/) record build and execution in emulated Linux. Use the native PACE jobs for milestone baseline measurements.
- The [batch script](../pace-baseline/stock-silo.sbatch) contains the documented build and test compatibility changes. Pinned source bundles are regenerated from Git when needed. The experiment files are stored as text in the repository.
