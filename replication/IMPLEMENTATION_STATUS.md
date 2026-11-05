# Implementation status

Last updated: November 5, 2026.

The supplied Silo checkout was inspected and exported at `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`. The PACE job and staging instructions are in [pace-baseline](pace-baseline/README.md). Stock TPC-C built and completed all ten benchmark measurements in PACE job `6791465`. The upstream suite passed in separate PACE job `6791730` with CPU selection adapted for the Slurm allocation. The CSV matches the archived raw output, and the test exit status and pass marker were verified in [results/pace-runs](results/pace-runs/). Ownership is proposed.

| Task | Description | Owner | Status | Evidence / blocker |
|---|---|---|---|---|
| I01 | Workspace and evidence conventions | Brian | Complete | Shared roadmap, tracker, provenance, PACE instructions, and results are in the Silo checkout. Original LaTeX source remains an input for M02 |
| I02 | Source inspection and experiment contract | Everyone | Complete — source audit and contract | Pinned PreemptDB `5582397`, mapped mechanisms and Q2 discrepancies, defined lifecycle/policy interfaces and configuration. [Source map](docs/source-map.md), [contract](docs/experiment-contract.md), [verification](results/i02-source-audit/README.md) |
| I03 | Machine qualification | Erin | Probe kit ready · qualified host pending | The Colima x86-64 guest reported `UINTR=0` on both vCPUs. The prior AWS VM also reported `UINTR=0`. PACE job 6791940 identified a Xeon Gold 6226, and candidate job 6792039 was submitted. A compatible host and kernel interface remain pending. [Runbook](uintr/README.md) · [Local probe evidence](results/mac03-uintr-kit/README.md) |
| I04 | Stock Silo build/tests | Brian | Validated on PACE | Job 6791465 produced all ten TPC-C measurements, then aborted at `rcu.cc:292` during test pinning. Job 6791730 completed the upstream suite with exit `0:0` after adapting the pinning test to the allocation's CPU mask. Jobs 6791273 and 6791405 exposed the Berkeley DB and GCC formatter build issues |
| I05 | Experiment runner and stock baseline | Erica | Complete | [Runner and analysis](experiments/README.md), [regenerated evidence](results/i05-experiments/README.md), and fixture tests. The new batch path has local runner validation and Bash syntax checks |
| I06 | Independent request driver | Erica | Implemented and locally validated | [Functional evidence and opt-in usage](results/i06-request-driver/README.md). The TPC-C bridge covers New-Order and Payment. The standalone suite uses synthetic low-priority work |
| I07 | Explicit context switching | Erin | Implemented and locally validated | Standalone glibc context pair passed one million round trips per build on emulated Linux x86-64. [ABI checks, lifecycle and reproduction commands](results/i07-worker-context/README.md) |
| I08 | Silo context state and protected regions | Brian / Erin | Implemented and locally validated | [Ownership audit, engine test and regression commands](results/i08-silo-context/README.md) |
| I09 | Mixed workload and Q2 validation | Erica / Brian | Implemented and locally validated | [Mixed profile, deterministic engine tests and smoke records](results/i09-mixed-workload/README.md). The one- and two-worker runs committed all 783 and 1,375 measured requests |
| I10 | Wait scheduler | Erica / Brian | Implemented and locally validated | [Deterministic scheduling test and four mixed runs](results/i10-wait/README.md). All 4,236 measurement requests committed, including 216 short requests |
| I11 | Cooperative scheduler | Brian | Implemented and locally validated | [Snapshot, interval and matched policy evidence](results/i11-cooperative/README.md). The eight runs committed 7,669 measurement requests. Cooperative recorded 38 mid-Q2 handoffs |
| I12 | Hardware interrupt delivery | Erin | Prepared · hardware validation pending | The standalone test for the UB-ADBLAB patched kernel compiled in the emulated guest and exited at its CPUID guard. Handler delivery and verified receiver resumption require a qualified Intel host. [Runbook](uintr/README.md) · [Local build and checks](results/mac03-uintr-kit/README.md) |
| I13 | Interrupt-driven switching | Erin / Brian | Local signal capture/return validated · native switch pending | MAC-09 passed 69 state checks (1,518 captures/returns), including independent arithmetic-interior capture, legacy FP/SIMD preservation, separate GPR/SIMD corruption controls, protected deferral and cleanup. Uses restricted assembly windows and the Linux kernel signal frame; native UINTR entry/return preservation remains open. [Contract](docs/linux-signal-backend.md) · [MAC-09 evidence](results/mac09-signal/README.md) |
| I14 | Silo Preemptive scheduler | Brian / Erin | Restricted local signal Q2 integration validated · native integration pending | MAC-09 added opt-in `linux-signal`, with per-worker real delivery and separate actual-capture/fallback counters. Nine Q2 transition cases preserved same-attempt checksums across 36 committed writes and 15 actual captures/resumptions; final Q2 fixtures also passed 53 real paused-write rollback/cleanup probes. Captures are confined to audited windows at completed reads; native UINTR integration remains MAC-06. [Results and limits](results/mac09-signal/README.md) |
| I15 | Batched preemption | Brian | Local model/signal batching validated · native pending | MAC-07 signal repetition is complete: final debug/optimized/UBSan plus 20 repetitions passed 11 cases each, and nine real Q2 transitions passed. Rejections, cancellation, duplicate/coalesced modeled events, actual sends/handlers, protection and service/return transitions remain separately accounted. Partial-admission/drain cases intentionally use boundary service; actual captures are required in the nine transition cases. Native hardware coalescing/return races remain MAC-06. [MAC-09 evidence](results/mac09-signal/README.md) |
| I16 | Starvation prevention | Brian | Local checkpoint/signal overload validated · native pending | MAC-08 signal repetition is complete. Final four-case suites passed all three builds plus 20 repetitions and real Q2; fixed a controlled-clock low-publication/reset race. Four signal overload runs reconciled 6,577 Q2 commits, 39,749 short commits and 32,227 rejections. At 25%: 7,315 forced returns / 518 admission blocks; at 100%: neither. Whole-request overshoot, local elapsed-ns timing and FIFO head blocking remain documented; native cycles/UINTR remain open. [MAC-09 results](results/mac09-signal/README.md) |
| I17 | Integrated validation | Everyone | Local signal workflow validated · full native validation pending | MAC-09 full validation fresh-built all three targets and passed state, driver, engine/context, 15 tool tests and four pressure runs. Final fixture-only follow-up passed all three builds plus 20 repetitions, signal/checkpoint engine modes and 53 actual paused-write abort probes. Linux/Mac replay reconciled 358 controlled blocks / 2,570 records and all 71,976 pressure short arrivals; reports regenerate identically. The full script was not repeated after fixture-only improvements; unchanged runtime/binary hashes are verified. Native I13–I16, remaining native stress/consistency matrix and upstream suite remain pending. [Evidence and failures](results/mac09-signal/README.md) |
| I18 | Replication experiments | Erica / everyone | Collection workflow and run plan prepared · native experiments pending | MAC-05 archived/replayed eight Wait/Cooperative runs (7,910 measurement commits) and independently checked per-type percentiles/counts. Figure 10 target and five varied three-policy orders are documented; Preemptive, individual CPU placement and native performance collection remain pending. [Evidence](results/mac05-workflow/README.md) · [Run plan](experiments/HOST_WORKFLOW.md#figure-10-style-native-run-plan) |
| M01 | Milestone evidence assessment | Everyone | In progress | Archived ten-run baseline, passing adapted stock suite, and I06–I08 functional evidence are ready to cite. Timeline assessment and hardware decision remain open |
| M02 | Milestone report | Everyone | Not started | Original LaTeX source not supplied |
| M03 | Milestone verification/package | Everyone | Not started | |
| F01 | Final report | Everyone | Not started | Final rubric/date not supplied |
| F02 | Final verification/package | Everyone | Not started | |
| X01–X03 | Optional extensions | Unassigned | Deferred | Not required for core work |

For each task update, record: source revision, files changed, commands/tests actually executed, results and raw evidence paths, outstanding limitations, and the next ready task. Do not mark remote validation complete from locally prepared scripts.

Local I04 evidence is in [results/i04-local-validation](results/i04-local-validation/). `stock-tpcc.csv` contains one five-second repetition at each worker count and `test.log` shows the fast checks before the slow-test timeout. Use these as build and execution evidence. For the milestone's baseline performance table, use the native PACE runs with the environment and repetition count reported.

## I02 completion record

Completed November 3, 2026 against Silo baseline `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`, documentation checkout `05a9229f4dc8c9e25485e34405b5e01f392b5c65`, Masstree gitlink `1150824e6a10a0384ba4bfed6b916ce62f96315f`, and PreemptDB `55823979233ecd833f1f4e10848de5e9bc0af667`. Added the source map, experiment contract and audit evidence. Updated provenance, README, roadmap and this tracker. The research plan already correctly attributes the 88–96% comparison to Wait.

Verification included reference clone/revision inspection, source symbol and workload review, reference file SHA-256 recording, Markdown link checks and `git diff --check`. Details and command results are in [audit evidence](results/i02-source-audit/README.md). The contract explicitly chooses corrected Q2 minimum selection, fixed scheduled arrivals with bounded rejection, stable retry identity, and monotonic timing. These choices require later implementation and runtime tests. Silo's Masstree submodule is uninitialized locally, exact publication-to-reference revision correspondence is unknown, and hardware qualification remains I03. I06 uses this contract.

## I05 completion record

Implemented November 3, 2026 on Silo source base `dc5529f16a17627b52994b6b22e399747b84fd10`. Added the JSON experiment runner, stock and queued configurations, analysis, SVG plotting, and a config-driven entry path in the existing stock PACE build job. The [I05 guide](experiments/README.md) explains the commands, manifests, latency definitions and CPU placement. The [I05 evidence](results/i05-experiments/README.md) links the regenerated tables and plots.

Ran the analysis and plotting commands on PACE job 6791465 and the two I06 smoke archives. All 50 stock metric values and both published summary rows matched. The queued parser reconciled 36 and 72 measurement commits and separately counted 36 one-worker warm-up commits. Ran `python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v`, `bash -n replication/pace-baseline/stock-silo.sbatch`, and `git diff --check`. The fixture tests cover percentile ranks, units, overload outcomes, drain, missing values, failed invocations and timeouts. The SVGs were rendered and inspected locally.

The existing native stock measurements supply the baseline. The config-driven Slurm invocation has yet to run on PACE. I07 context switching and I09 workload work are ready independently. I03 still owns UINTR qualification.

## I06 completion record

Implemented November 3, 2026 on Silo source base `05d1c2e4b1050465353efd68d1abc6c57d649b17`. Added `benchmarks/request_driver.{h,cc}`, opt-in dispatch in `benchmarks/bench.{h,cc}` and `benchmarks/dbtest.cc`, the initial TPC-C adapter in `benchmarks/tpcc.cc`, and the standalone test in `replication/tests/request_driver_test.cc`. The producer accounts for scheduled arrivals and queue-full rejection independently of worker completion. Each worker consumes its own bounded queues and keeps the same input seed, warehouse, logical date and request ID across retries. Output includes terminal records, a cutoff reconciliation and drain counts.

The [I06 evidence](results/i06-request-driver/README.md) records commands, environment, and logs. The standalone suite passed on macOS and emulated Linux, with AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer checks on macOS. It covers missed arrivals, rejection, cancellation, duplicate wakeups, retries, fatal errors, and request reconciliation across three workers. In the Linux slow-worker test, the short request spent 80.42 ms in the local queue and 82.73 ms end to end. The emulated Linux build passed the stock five-column smoke and committed all 36 and 72 measured requests in the one- and two-worker queued smokes. The one-worker run also drained 36 warm-up requests before measurement. These are functional checks.

I07 and I09 became ready after I06. Real Q2, full input materialization, mid-transaction switching, policy comparisons and native queued PACE measurements remain later work.

## I07 completion record

Implemented November 3, 2026 on Silo source base `70f5c73a9dcd368c2717af52ad0298ff2b8f007e`. Added [WorkerContext](../benchmarks/worker_context.h) and its glibc-backed implementation in [worker_context.cc](../benchmarks/worker_context.cc). A worker resumes an entry function on a guarded alternate stack. The entry can yield back to the original stack and resume at the same call site. Returning or unwinding the entry transfers control to the original stack for cleanup. The pair shares pthread identity and TLS.

Built and ran the [standalone harness](tests/worker_context_test.cc) with GCC 11.4.0 and glibc 2.35 on an emulated Linux x86-64 guest. Debug, optimized and UndefinedBehaviorSanitizer builds each passed 1, 10,000 and 1,000,000 round trips, including independent stack values, SysV nonvolatile GPRs, callback stack alignment, x87 rounding control and MXCSR. Lifecycle, exception cleanup, thread ownership and signal-mask tests also passed. [Commands, results table and raw log](results/i07-worker-context/README.md).

I08 uses this voluntary switching primitive. Native PACE execution, UINTR delivery and asynchronous register preservation remain separate validation tasks.

## I08 completion record

Implemented November 3, 2026 on Silo source base `8b6db36123a24958c97589adf63516cdc68bc30c`. Added [context-owned benchmark state](../benchmarks/bench_txn_context.h), [nested worker protection](../preemption.h), and the [two-context engine test](tests/silo_context_test.cc). The Silo adapter protects the transaction and index call boundaries. A live write attempt stays protected through cleanup, and a paused read-only snapshot retains the worker's RCU guard. Deferred service runs at the next explicit safe checkpoint or attempt boundary. A failed commit keeps the object alive for the caller's abort cleanup.

On emulated Linux x86-64, the engine test passed four optimized runs and one debug run with Silo and Masstree invariants enabled. Stock and queued TPC-C passed smoke checks, and the I06 request driver and I07 switching suites passed. The [I08 audit and validation](results/i08-silo-context/README.md) link the raw outputs and commands. I09 supplies Q2. I11 connects safe checkpoints to the Cooperative scheduler after I10's Wait implementation.

## I09 completion record

Implemented November 4, 2026 on Silo source base `642e51553f870f56ec6f22126874a10c273a12d6`. Updated the request driver, benchmark dispatch and [TPC-C adapters](../benchmarks/tpcc.cc) to materialize queued input once and retain it across retries. Added [Q2](../benchmarks/tpcc_q2.h) as a read-only snapshot over Silo's stock and item indexes, the [mixed configuration](experiments/configs/queued-mixed-smoke.json), checksum handling in the result writer and analyzer, and the [engine harness](tests/tpcc_mixed_test.cc). Queued Payment history uses the customer column order declared by the schema and loader.

The emulated Linux x86-64 build passed the deterministic Q2 and short-transaction checks, the stock and short-only smokes, and both mixed smokes. The measurement phases committed 783 requests with one worker and 1,375 with two workers, including 747 and 1,303 Q2s. The I06 driver test and nine experiment-tool tests passed. [Commands, environment, raw records and reconciliation](results/i09-mixed-workload/README.md)

I10 uses this workload for the Wait baseline.

## I10 completion record

Implemented November 4, 2026 on Silo source base `1d763ee97838d1593ef80b30e1c48ec3572a4910`. Added `--queued-policy wait` to [dbtest](../benchmarks/dbtest.cc), made the [queued driver's](../benchmarks/request_driver.cc) high-first dispatch boundary explicit, and added a Wait setting to the I05 runner and [mixed configuration](experiments/configs/queued-wait-smoke.json). The deterministic [driver test](tests/request_driver_test.cc) verified that two shorts waited 7 and 8 µs behind an active long request, then ran in FIFO order ahead of the next long request.

The standalone driver, sanitizer checks, ten experiment-tool tests, and Linux TPC-C engine harness passed. The I05 runner completed two repetitions each at one and two workers on emulated Linux x86-64. The four measured phases committed 4,236 requests, including 216 New-Order and Payment requests and 4,020 Q2 requests. The archived traces reconcile every phase and show serial execution per worker. [Commands, manifests, raw traces and analysis](results/i10-wait/README.md)

I11 uses Wait as the comparator in a fresh paired batch on the same binary.

## I11 completion record

Implemented November 4, 2026 on Silo source base `834a3f8e4421cf605c2a8cbd1b99c83e829930d0`. Added Cooperative and its configurable record interval to [dbtest](../benchmarks/dbtest.cc) and the [request driver](../benchmarks/request_driver.cc). The [Q2 adapter](../benchmarks/tpcc_q2.h) checks completed supplier, stock and item lookups after index protection is released. The benchmark worker uses separate low-priority transaction storage and preserves the active snapshot across high-priority service.

The Linux driver and engine tests verified exact intervals, empty queues, nested deferral, repeated handoffs, retry and abort accounting, and the expected Q2 checksum across nine writes. The I05 runner completed four matched policy pairs at one and two workers. Every measured request committed. Cooperative serviced 152 short requests during 38 Q2 handoffs, and Wait ran all shorts between Q2s. [Commands, raw evidence and comparison](results/i11-cooperative/README.md)

The comparison used emulated Linux x86-64, one-second measurement windows and a 64-read smoke interval. The 10,000-read default exceeded the Q2 size in the separate default-interval smoke. Native PACE comparisons remain open. I12 requires I03 qualification and an approved UINTR interface.

## MAC-09 local signal validation record

November 5, 2026, source base `c7989984e6e6795d88046157cef7c6d40e33d133` plus
archived changes. MAC-09 and the pending MAC-07/MAC-08 signal repetitions are
complete locally for the documented restricted windows/state. Exact commands,
source/binary hashes, failed attempts, final fixture follow-up, raw records and
replay are in [MAC-09 evidence](results/mac09-signal/README.md); the shared
[local table](LOCAL_REPLICATION_PLAN.md#current-status) records the resulting
local task statuses. These runs do not complete native I12–I18 or establish a
matched three-policy performance comparison. MAC-10 acceptance assessment and
MAC-11 report/package remain local next steps; MAC-06 owns native qualification,
integration and final experiments.
