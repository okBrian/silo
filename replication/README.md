# PreemptDB replication on Silo

This directory contains the CS 6400 project roadmap, PACE workflow, and evidence collected so far.

- [Implementation roadmap](IMPLEMENTATION_PLAN.md)
- [Progress tracker](IMPLEMENTATION_STATUS.md)
- [Original milestone plan](research-and-milestone-plan.md)
- [Source and evidence provenance](docs/provenance.md)
- [I02 source map and Q2 audit](docs/source-map.md)
- [Shared experiment and implementation contract](docs/experiment-contract.md)
- [I02 audit evidence](results/i02-source-audit/README.md)
- [I05 experiment runner and analysis](experiments/README.md)
- [I05 regenerated tables and plots](results/i05-experiments/README.md)
- [I06 queued driver usage and functional validation](results/i06-request-driver/README.md)
- [Run the stock Silo baseline on PACE](pace-baseline/README.md)
- [PACE TPC-C results and summary](results/i04-pace-baseline/SUMMARY.md)
- [PACE benchmark logs](results/pace-runs/6791465/) and [test rerun logs](results/pace-runs/6791730/)
- [Local smoke validation](results/i04-local-validation/README.md)

The PACE benchmark completed all ten runs, and the separate test job passed after adapting CPU selection to the Slurm allocation. UINTR qualification is in progress. The experiment job applies its compatibility changes to a temporary build copy and writes a diff into the run directory.
