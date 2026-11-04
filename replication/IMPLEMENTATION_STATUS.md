# Implementation status

Last updated: November 3, 2026.

The supplied Silo checkout was inspected and exported at `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`. The PACE job and staging instructions are in [pace-baseline](pace-baseline/README.md). Stock TPC-C built and completed all ten benchmark measurements in PACE job `6791465`. The upstream suite passed in separate PACE job `6791730` with CPU selection adapted for the Slurm allocation. The CSV matches the archived raw output, and the test exit status and pass marker were verified in [results/pace-runs](results/pace-runs/). Ownership is proposed.

| Task | Description | Owner | Status | Evidence / blocker |
|---|---|---|---|---|
| I01 | Workspace and evidence conventions | Brian | Complete | Shared roadmap, tracker, provenance, PACE instructions, and results are in the Silo checkout. Original LaTeX source remains an input for M02 |
| I02 | Source inspection and experiment contract | Everyone | In progress | Recorded Silo/Masstree revisions and mapped Silo benchmark entry points. PreemptDB reference and interface audit pending |
| I03 | Machine qualification | Erin | GPU allocation queued | Job 6791940 confirmed the unlabeled `coc-cpu` host was Xeon Gold 6226. The CPU-only request on `ice-gpu` was rejected. The one-GPU request targeting Xeon Platinum 8462Y+ was accepted as job 6792039 and is pending. CPUID and interface availability remain unverified |
| I04 | Stock Silo build/tests | Brian | Validated on PACE | Job 6791465 produced all ten TPC-C measurements, then aborted at `rcu.cc:292` during test pinning. Job 6791730 completed the upstream suite with exit `0:0` after adapting the pinning test to the allocation's CPU mask. Jobs 6791273 and 6791405 exposed the Berkeley DB and GCC formatter build issues |
| I05 | Experiment runner and stock baseline | Erica | Stock baseline summarized | Saved PACE CSV and summary in `results/i04-pace-baseline/`. Native environment files and logs are archived. Generic experiment tooling and queued workload analysis remain open |
| I06 | Independent request driver | Erica | Not started | |
| I07 | Explicit context switching | Erin | Not started | |
| I08 | Silo context state and protected regions | Brian / Erin | Not started | |
| I09 | Mixed workload and Q2 validation | Erica / Brian | Not started | |
| I10 | Wait scheduler | Erica / Brian | Not started | |
| I11 | Cooperative scheduler | Brian | Not started | |
| I12 | Hardware interrupt delivery | Erin | Not started | Requires qualified host/interface |
| I13 | Interrupt-driven switching | Erin / Brian | Not started | |
| I14 | Silo Preemptive scheduler | Brian / Erin | Not started | |
| I15 | Batched preemption | Brian | Not started | |
| I16 | Starvation prevention | Brian | Not started | |
| I17 | Integrated validation | Everyone | Not started | |
| I18 | Replication experiments | Erica / everyone | Not started | |
| M01 | Milestone evidence assessment | Everyone | In progress | Archived ten-run baseline and passing adapted test suite are ready to cite. Component evidence, timeline assessment, and hardware decision remain open |
| M02 | Milestone report | Everyone | Not started | Original LaTeX source not supplied |
| M03 | Milestone verification/package | Everyone | Not started | |
| F01 | Final report | Everyone | Not started | Final rubric/date not supplied |
| F02 | Final verification/package | Everyone | Not started | |
| X01–X03 | Optional extensions | Unassigned | Deferred | Not required for core work |

For each task update, record: source revision, files changed, commands/tests actually executed, results and raw evidence paths, outstanding limitations, and the next ready task. Do not mark remote validation complete from locally prepared scripts.

Local I04 evidence is in [results/i04-local-validation](results/i04-local-validation/). `stock-tpcc.csv` contains one five-second repetition at each worker count and `test.log` shows the fast checks before the slow-test timeout. Use these as build and execution evidence. For the milestone's baseline performance table, use the native PACE runs with the environment and repetition count reported.
