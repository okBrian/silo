# PreemptDB replication on Silo

This directory contains the CS 6400 project roadmap, PACE workflow, and evidence collected so far.

- [Implementation roadmap](IMPLEMENTATION_PLAN.md)
- [Progress tracker](IMPLEMENTATION_STATUS.md)
- [Original milestone plan](research-and-milestone-plan.md)
- [Source and evidence provenance](docs/provenance.md)
- [Run the stock Silo baseline on PACE](pace-baseline/README.md)
- [PACE TPC-C results and summary](results/i04-pace-baseline/SUMMARY.md)
- [PACE benchmark logs](results/pace-runs/6791465/) and [test rerun logs](results/pace-runs/6791730/)
- [Local smoke validation](results/i04-local-validation/README.md)

The PACE benchmark completed all ten runs, and the separate test job passed after adapting CPU selection to the Slurm allocation. UINTR qualification is in progress. The experiment job applies its compatibility changes to a temporary build copy and writes a diff into the run directory.
