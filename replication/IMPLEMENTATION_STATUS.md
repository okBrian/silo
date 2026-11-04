# Implementation status

Last updated: November 3, 2026.

The supplied Silo checkout was inspected and exported at `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`. The PACE job and staging instructions are in [pace-baseline](pace-baseline/README.md). Stock TPC-C built and completed all ten benchmark measurements in PACE job `6791465`. The upstream suite passed in separate PACE job `6791730` with CPU selection adapted for the Slurm allocation. The CSV matches the archived raw output, and the test exit status and pass marker were verified in [results/pace-runs](results/pace-runs/). Ownership is proposed.

| Task | Description | Owner | Status | Evidence / blocker |
|---|---|---|---|---|
| I01 | Workspace and evidence conventions | Brian | Complete | Shared roadmap, tracker, provenance, PACE instructions, and results are in the Silo checkout. Original LaTeX source remains an input for M02 |
| I02 | Source inspection and experiment contract | Everyone | Complete — source audit and contract | Pinned PreemptDB `5582397`, mapped mechanisms and Q2 discrepancies, defined lifecycle/policy interfaces and configuration. [Source map](docs/source-map.md), [contract](docs/experiment-contract.md), [verification](results/i02-source-audit/README.md) |
| I03 | Machine qualification | Erin | GPU allocation queued | Job 6791940 confirmed the unlabeled `coc-cpu` host was Xeon Gold 6226. The CPU-only request on `ice-gpu` was rejected. The one-GPU request targeting Xeon Platinum 8462Y+ was accepted as job 6792039 and is pending. CPUID and interface availability remain unverified |
| I04 | Stock Silo build/tests | Brian | Validated on PACE | Job 6791465 produced all ten TPC-C measurements, then aborted at `rcu.cc:292` during test pinning. Job 6791730 completed the upstream suite with exit `0:0` after adapting the pinning test to the allocation's CPU mask. Jobs 6791273 and 6791405 exposed the Berkeley DB and GCC formatter build issues |
| I05 | Experiment runner and stock baseline | Erica | Stock baseline summarized | Saved PACE CSV and summary in `results/i04-pace-baseline/`. Native environment files and logs are archived. Generic experiment tooling and queued workload analysis remain open |
| I06 | Independent request driver | Erica | Implemented and locally validated | [Functional evidence and opt-in usage](results/i06-request-driver/README.md). The TPC-C bridge covers New-Order and Payment. The standalone suite uses synthetic low-priority work |
| I07 | Explicit context switching | Erin | Not started | |
| I08 | Silo context state and protected regions | Brian / Erin | Not started | |
| I09 | Mixed workload and Q2 validation | Erica / Brian | Initial short-transaction bridge available | I06 exercises New-Order and Payment dispatch. Q2, full input materialization and workload correctness checks remain open |
| I10 | Wait scheduler | Erica / Brian | Not started | |
| I11 | Cooperative scheduler | Brian | Not started | |
| I12 | Hardware interrupt delivery | Erin | Not started | Requires qualified host/interface |
| I13 | Interrupt-driven switching | Erin / Brian | Not started | |
| I14 | Silo Preemptive scheduler | Brian / Erin | Not started | |
| I15 | Batched preemption | Brian | Not started | |
| I16 | Starvation prevention | Brian | Not started | |
| I17 | Integrated validation | Everyone | Not started | |
| I18 | Replication experiments | Erica / everyone | Not started | |
| M01 | Milestone evidence assessment | Everyone | In progress | Archived ten-run baseline, passing adapted stock suite, and I06 functional evidence are ready to cite. Timeline assessment and hardware decision remain open |
| M02 | Milestone report | Everyone | Not started | Original LaTeX source not supplied |
| M03 | Milestone verification/package | Everyone | Not started | |
| F01 | Final report | Everyone | Not started | Final rubric/date not supplied |
| F02 | Final verification/package | Everyone | Not started | |
| X01–X03 | Optional extensions | Unassigned | Deferred | Not required for core work |

For each task update, record: source revision, files changed, commands/tests actually executed, results and raw evidence paths, outstanding limitations, and the next ready task. Do not mark remote validation complete from locally prepared scripts.

Local I04 evidence is in [results/i04-local-validation](results/i04-local-validation/). `stock-tpcc.csv` contains one five-second repetition at each worker count and `test.log` shows the fast checks before the slow-test timeout. Use these as build and execution evidence. For the milestone's baseline performance table, use the native PACE runs with the environment and repetition count reported.

## I02 completion record

Completed November 3, 2026 against Silo baseline `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`, documentation checkout `05a9229f4dc8c9e25485e34405b5e01f392b5c65`, Masstree gitlink `1150824e6a10a0384ba4bfed6b916ce62f96315f`, and PreemptDB `55823979233ecd833f1f4e10848de5e9bc0af667`. Added the source map, experiment contract and audit evidence. Updated provenance, README, roadmap and this tracker. The research plan already correctly attributes the 88–96% comparison to Wait.

Verification included reference clone/revision inspection, source symbol and workload review, reference file SHA-256 recording, Markdown link checks and `git diff --check`. Details and command results are in [audit evidence](results/i02-source-audit/README.md). The contract explicitly chooses corrected Q2 minimum selection, fixed scheduled arrivals with bounded rejection, stable retry identity, and monotonic timing. These choices require later implementation and runtime tests. Silo's Masstree submodule is uninitialized locally, exact publication-to-reference revision correspondence is unknown, and hardware qualification remains I03. I06 uses this contract. I07 and the remaining I05 runner work can proceed independently.

## I06 completion record

Implemented November 3, 2026 on Silo source base `05d1c2e4b1050465353efd68d1abc6c57d649b17`. Added `benchmarks/request_driver.{h,cc}`, opt-in dispatch in `benchmarks/bench.{h,cc}` and `benchmarks/dbtest.cc`, the initial TPC-C adapter in `benchmarks/tpcc.cc`, and the standalone test in `replication/tests/request_driver_test.cc`. The producer accounts for scheduled arrivals and queue-full rejection independently of worker completion. Each worker consumes its own bounded queues and keeps the same input seed, warehouse, logical date and request ID across retries. Output includes terminal records, a cutoff reconciliation and drain counts.

The [I06 evidence](results/i06-request-driver/README.md) records commands, environment, and logs. The standalone suite passed on macOS and emulated Linux, with AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer checks on macOS. It covers missed arrivals, rejection, cancellation, duplicate wakeups, retries, fatal errors, and request reconciliation across three workers. In the Linux slow-worker test, the short request spent 80.42 ms in the local queue and 82.73 ms end to end. The emulated Linux build passed the stock five-column smoke and committed all 36 and 72 measured requests in the one- and two-worker queued smokes. The one-worker run also drained 36 warm-up requests before measurement. These are functional checks.

Next ready work is I07 or the independent I09 workload work, with the remaining I05 experiment tooling in parallel. Real Q2, full input materialization, mid-transaction switching, policy comparisons and native PACE measurements remain later work.
