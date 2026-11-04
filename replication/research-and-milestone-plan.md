# PreemptDB replication: research and milestone plan

This is the original milestone planning document. See the [progress tracker](IMPLEMENTATION_STATUS.md) for the current state.

Prepared November 2, 2026 for Erin Tan, Brian Ok, and Erica Tay.

**Working deadline:** within 48 hours, approximately November 4; check Canvas for the exact submission time. The final-report deadline is not yet known. This is a proposed work plan, not a record of completed implementation or experiments. Use “Erica Tay,” as specified in the latest group information, when updating the proposal's author list.

**Recommendation:** retain the proposed Silo implementation, but target a smaller, evidence-based milestone: a reproducible Silo build, working baseline measurements, and validation of one new component such as the independent request driver or explicit context switching. User-interrupt integration is a stretch goal for these two days. Do not let obtaining privileged machine access block all other work.

The proposal promised a working interrupt-capable machine, two contexts per Silo worker, tested context-local state, and measured Wait and Cooperative baselines by the milestone. Without evidence that these already exist, that full target is not a credible two-day commitment. Report the actual gap and recovery plan. The assignment explicitly allows incomplete and negative results when supported by evidence and analysis.

**1. Define the research question before coding**

Test whether preemption reduces high-priority New-Order latency in a mixed short/long workload on Silo, while examining the effect on Q2 completion, throughput, and aborts. Treat this as an independent implementation and a test of whether the mechanism generalizes across engines. Silo's different concurrency control can change both performance and the direction of the result; matching the trend is a hypothesis, not a guarantee.

Keep Figure 10 as the primary final target. Correct the proposal's claim: the paper reports an 88–96% latency reduction relative to **Wait**, not Cooperative. This is a published observation, not a pass/fail threshold for your implementation. [Paper, Section 6.3](https://www.cs.sfu.ca/~tzwang/preemptdb.pdf)

Use the public PreemptDB repository to understand implementation details and validate workload semantics. Clearly distinguish code inspected, code reused, and code independently implemented. Running the authors' system can be a useful reference experiment, but does not establish completion of the proposed Silo port. If you decide to replace the Silo port with artifact reproduction, disclose that substantial scope change and discuss it with the instructor.

Defer AMD comparisons, adaptive starvation thresholds, and distributed interrupt delivery until the core experiment is complete. Do not spend milestone time implementing extensions.

**2. First two hours: establish the common foundation**

- All three members: spend 30–45 minutes reading Sections 4–5 for mechanisms, then Section 6.1 and Figure 10 for the experiment. Each person writes the inputs, outputs, and validation criterion for their assigned component.
- Brian: create the shared development checkout, record the exact Silo revision and submodule revisions, and attempt an unchanged build and correctness suite before modifying code.
- Erin: obtain an allocated PACE compute-node session and record its CPU model, kernel, compiler, available memory, allocated CPU set, and job identifier. Check user-interrupt capability on the allocated node, not merely the login node. [PACE job orientation](https://docs.pace.gatech.edu/training/img/PACE_orientation_Aug2023.pdf)
- Erica: inspect Silo's existing TPC-C driver and prepare the results directory, run log, and a parser for actual benchmark output. Start the report in a copy of the original proposal's LaTeX source.
- Agree on one request representation, including request ID, transaction type, inputs, generation time, first execution time, completion time, and retry count. Agree on which thread owns each queue.

Only the proposal PDF was supplied here; its original LaTeX project is still needed to reuse the exact template. Retrieve it from the group's original Overleaf/local project, including bibliography and class/style files.

**3. Hardware decision: Erin owns a six-hour investigation, not a two-day wait**

There are three separate questions: does the CPU support UINTR, does the running system expose a usable interface, and is its use permitted in your allocation? A CPU capability result alone does not establish that the project can run.

PreemptDB documents both a patched kernel and an alternative character-device interface. The latter still uses a kernel module and its installation instructions require administrative actions; it is not a way around shared-cluster restrictions. [PreemptDB setup](https://github.com/sfu-dis/preemptdb), [driver installation](https://github.com/UB-ADBLAB/uintr-driver)

Today's actions:

1. Run the authors' linked CPU capability checker on an allocated node and save its result. Record any build failure rather than interpreting it as lack of CPU support. [Capability checker](https://github.com/UB-ADBLAB/check_uintr)
2. Ask the course staff/PACE support route whether an approved UINTR-enabled kernel or driver is already available, whether the needed hugepage configuration is available, and whether a suitable dedicated host can be arranged. Specify that this is a two-day course milestone and that ordinary baseline jobs can proceed independently.
3. If an approved UINTR interface exists, run a minimal sender/receiver test before attempting database integration. Save successful delivery and return-to-computation evidence.
4. At hour 6, classify access as **working**, **unsupported**, or **unconfirmed**. If unconfirmed, continue the milestone without relying on it. An unanswered support request remains an unresolved dependency.

Fallback for the milestone: use ordinary allocated PACE CPU resources for Silo tests, stock TPC-C measurements, request-queue tests, and explicit context switching. These do not establish hardware preemption, but can provide useful component evidence.

Fallback for the final: identify an institution-provided or approved rented machine with verified UINTR exposure and permission to configure its kernel/driver. A generic cloud VM is only a candidate; confirm capability and privileges before committing money or the experiment schedule. If no suitable host is secured within three days after the milestone, bring a reduced-scope proposal to the instructor while continuing baseline work. Do not silently relabel signals or cooperative yielding as UINTR replication.

**4. Two-day work allocation and checkpoints**

These are proposed assignments based on the proposal, not claims about work already performed.

| Window | Erin Tan | Brian Ok | Erica Tay | Required checkpoint |
|---|---|---|---|---|
| Hours 0–6 | PACE inventory, capability check, access inquiry | Unchanged Silo build and test suite; note dependencies and local fixes | Stock TPC-C smoke run; run-log and result format; report outline | Recorded environment, build outcome, and hardware classification |
| Hours 6–18 | Small explicit two-context switch test, without interrupts | Audit transaction-specific thread-local state; review integration boundaries | Independent request-generation/queue prototype with timestamp validation | At least one new component has a reproducible test and saved output |
| Hours 18–26 | Test repeated switching and state isolation; actual UINTR delivery only if available | Help integrate the request driver with a minimal Wait path if ready | Repeat stock baseline; validate request accounting; small Wait experiment if ready | Freeze the evidence set available for the report |
| Hours 26–36 | Write hardware findings and context-test results | Write implementation status and integration risks | Produce tables/plots from saved logs; write methods and results | Complete first report draft |
| Hours 36–44 | Review technical claims and feedback response | Review correctness and recovery schedule | Check plots, units, and LaTeX layout | Every completion claim has supporting evidence |
| Hours 44–48 | Joint review | Joint review | Joint review; designated submitter uploads | Final PDF, verified submission, retained logs and source |

Work in parallel, but protect the last 12 hours for writing and review. Do not make a new assembly/context-switch integration a dependency of the report on the last evening.

If unchanged Silo still cannot build by hour 8, Brian time-boxes dependency debugging and records the blocker; Erin and Erica continue independent component tests. If the new driver cannot be integrated by hour 24, use validated standalone driver tests and clearly labeled stock Silo measurements. This is less complete than the proposal but defensible evidence of progress.

**5. What to implement and validate first**

**Brian: establish the engine baseline.** Build the unchanged engine, run its supplied tests, and save command lines and exit status. The Silo README documents a debug/invariant build and a performance benchmark build. An upstream test pass validates the starting engine; it is not evidence that your scheduling changes are correct. [Silo build and test instructions](https://github.com/stephentu/silo)

After the baseline works, identify the state that must become context-local: current transaction, scratch space, allocator-related state, and other transaction-specific thread-local objects. Write an audit table with the object, current owner/lifetime, proposed owner, and unresolved issue. The audit is design progress, not experimental validation by itself.

**Erin: validate voluntary switching before asynchronous preemption.** Start with two private stacks/contexts, distinct local values, a known computation, and repeated explicit switches. Proposed target: 100,000 or more round trips with the expected final values and no state-isolation failures. Record the actual achieved count. If you use an existing switching library or reference implementation initially, label that dependency and the limited claim it supports.

Check stack alignment, required register state, and context-local values. A voluntary switch test does not establish that interrupt-time register saving is correct. Test interrupt-driven preservation separately later. For protected regions, add a nested-entry/exit test once the implementation exists: switching must remain disallowed until the outermost region exits, and pending work must eventually be serviced according to the chosen policy.

**Erica: validate measurement before interpreting latency.** Generate requests independently of worker completion and assign unique IDs. Use a controlled slow worker and inject a short request while it is busy. Verify that the short request's recorded latency includes its waiting time. A deterministic simulated-clock test can verify bookkeeping; label it as a functional test, not a performance measurement.

For each completed logical request:

- scheduling delay = first execution time − generation time;
- end-to-end latency = successful completion time − generation time;
- if a transaction retries, preserve the original request ID and generation timestamp, and count retries separately.

Validate no duplicate completions, missing IDs, negative intervals, or unexplained discarded requests. Reconcile generated requests with completed, outstanding, rejected, and dropped requests. Save offered and achieved request rates so an overloaded system cannot look fast merely by omitting slow requests.

**6. Minimum experimental evidence to aim for**

| Evidence | Proposed experiment | What it demonstrates | What it does not demonstrate |
|---|---|---|---|
| Silo starting point | Supplied correctness tests plus a small TPC-C run | Engine builds and executes on your environment | New scheduling mechanisms are correct |
| Stock baseline | One and two workers; five 30-second measured runs per configuration after a documented warm-up | Initial throughput, variability, and feasible dataset size | Mixed-workload scheduling latency or Figure 10 replication |
| Driver validation | Controlled queued requests, timestamp checks, ID/accounting checks | Queuing is represented and measurements are internally consistent | Preemption is effective |
| Context validation | Repeated explicit switches with independent state and known results | Tested switching/state behavior works | Asynchronous interrupt handling is correct |
| Optional Wait result | One worker, short requests plus the validated long workload | Initial mixed-workload behavior | Full three-policy replication |
| Stretch only | Small matched Wait/Cooperative comparison, or successful UINTR sender/receiver test | Narrow additional progress, as documented | Completed Silo preemption integration |

The repetition counts are planning choices, not measured results or course requirements. Use a smaller dataset if necessary and record the choice. If stock Silo does not report a desired latency metric, report the throughput it actually emits rather than inventing a percentile. A synthetic long task is acceptable for driver validation, but must be labeled synthetic, not the paper's Q2 workload.

A credible milestone evidence package would contain a baseline table, one component-validation table, and optionally one small plot. If performance evaluation is not ready, prioritize substantial component-validation evidence; that is explicitly permitted by the assignment. Hardware inspection and literature review alone are too weak as the intended technical evidence.

**7. Plan the final experiment while keeping this milestone small**

Freeze a written configuration before the final comparison. The paper's default setup includes 16 workers, high/low-priority queue capacities of 4/1 per worker, 1 ms arrivals, 30-second runs, and a Cooperative interval of 10,000 record reads. Record every departure. [Paper, Section 6.1](https://www.cs.sfu.ca/~tzwang/preemptdb.pdf)

For the proposed Silo study:

- Validate the Q2 variant against the public benchmark logic on a tiny deterministic dataset before performance experiments; document any remaining approximation.
- Keep the engine, transaction code, isolation behavior, initial data, worker count, CPU placement, input distributions, logging configuration, and measurement path matched across Wait, Cooperative, and Preemptive runs.
- Inspect request-generation semantics carefully: a fixed interval may generate a batch, so the interval alone does not determine offered request rate. Record batch size and rate explicitly.
- Use the same saved request inputs or reproducible seeds where applicable. Record warm-up, measurement, and draining rules. Do not mix post-measurement draining time into the throughput denominator without documenting it.
- Report New-Order and Q2 p50/p90/p99/p99.9, committed throughput by transaction type, aborts/retries, and outstanding work. Keep the request's original timestamp across retries.
- Save raw samples and sample counts. With N samples, about 0.001N lie beyond p99.9: 10,000 samples give only about ten observations in that tail. Mark small-sample tail estimates as preliminary, and collect more samples before strong conclusions.
- Repeat conditions, vary their run order, and report variability. Do not average per-run percentiles and present the result as the percentile of all requests.
- Compare scheduling policies within Silo first; use the paper as an external reference. Differences between Silo and the paper's engine are possible explanations to investigate, not noise to hide.

**8. Build the milestone report around the assignment's four requirements**

Use the original LaTeX template. A suggested allocation is roughly half the space for concrete validation/results, with the remaining space divided between status, methodology changes, and risks; follow any separate course page limit.

**Progress check-in:** include a component table with owner, completed/in progress/not started, evidence file or run ID, and next action. Populate status from the actual two-day work. State what was promised by the original milestone and what remains unfinished. Describe the work performed by the team separately from upstream code reused.

Suggested rows: machine qualification; Silo build/tests; independent driver; workload/Q2 validation; Wait; Cooperative; explicit switching; context-local storage; interrupt delivery; protected regions; batched preemption; starvation prevention; evaluation tooling.

**Preliminary validation and results:** describe the environment, exact tested component, setup, result, and limitation. Include real test counts and actual measured numbers. For each result, explain what it supports about the replication goal. A failed build or negative result should include its cause or current diagnostic evidence and the next investigation; do not replace all validation with screenshots of error messages if functional testing is feasible elsewhere.

**Evolving methodology:** reproduce the proposal's Replication Plan and mark changes using the template-compatible `changes` package or another clear change notation. Proposed changes to record are:

- a time-bounded machine qualification gate and a concrete compatible-host fallback;
- a reduced milestone scope focused on components and small baseline experiments;
- smaller preliminary configurations before the final target configuration;
- the corrected Wait comparator for the 88–96% published result;
- stronger request accounting and explicit distinction between author-code results and independent Silo results;
- deferral of optional extensions.

Justify changes with actual findings. Do not state that a driver is usable on PACE or that hardware risk is resolved without a successful approved test.

**Response to proposal feedback:** write 1–2 sentences stating what machine qualification was actually attempted, its outcome, and the fallback's current status. For example, describe the measured CPU/interface result and the deadline for obtaining an approved alternative. If support is still pending, say so. A planned cloud fallback is not an acquired working machine.

**Risks and mitigation:** limit remaining risks to the following two broad items.

| Remaining risk | Trigger | Concrete mitigation |
|---|---|---|
| No usable UINTR machine | No working approved interface by hour 6; still none three days after milestone | Complete PACE component/baseline work for the milestone; pursue verified compatible dedicated hardware; discuss reduced final scope with instructor at the later trigger |
| Silo integration or concurrency behavior prevents a credible comparison | State corruption, failed consistency tests, excessive Q2 aborts, or unstable matched measurements | Integrate one mechanism at a time; preserve baseline; diagnose on tiny workloads; report abort/retry behavior; drop extensions and reduce experiment matrix before changing transaction semantics |

Only list a risk as addressed when the relevant validation actually passes. Finding public source code reduces implementation ambiguity; it does not prove correctness or machine feasibility.

**9. Recovery schedule after the milestone**

The proposal places the final report in course Week 16. Confirm the actual date now and map these targets onto it. These are conditional targets, not promises that the remaining low-level work will take exactly this long.

| Target | Deliverable and decision |
|---|---|
| Milestone + 3 days | Working approved UINTR host, or instructor discussion of reduced scope |
| Milestone + 7 days | Validated workload/Wait driver and explicit two-context Silo integration; context-local state and protection tests |
| Milestone + 14 days | Cooperative baseline and small interrupt-driven Silo run; batching and starvation behavior tested |
| At least 7 days before final | Freeze core implementation; run matched main comparison and required repeats; add sensitivity experiments only if time permits |
| At least 3 days before final | Freeze results; complete discrepancy analysis, reproducibility instructions, and report |

If the actual final deadline makes these checkpoints overlap, reduce the experiment matrix and extensions immediately and discuss any change to the core replication objective. Do not rely on untested code at the measurement freeze.

**10. Evidence and submission checklist**

Keep a shared folder containing source revisions and local diffs, build commands, environment/job information, test commands and outputs, benchmark configurations, raw logs, analysis scripts, figures, and the report source. Every table row should be traceable to a run or test.

Before submission, check that the report:

- identifies each member's actual contribution and each component's actual status;
- presents concrete experimental evidence without placeholder numbers;
- distinguishes stock Silo, standalone components, the Silo port, and author-code runs;
- honestly assesses progress against the original plan;
- highlights methodology changes and answers the machine-access feedback;
- contains no more than two remaining risks, each with an action and trigger;
- preserves the proposal template and uses the correct author names;
- compiles to a readable PDF with legible tables and figures;
- is uploaded once by the agreed group submitter before the confirmed Canvas deadline.

**Immediate priority for Erin:** start the allocated-node hardware check and explicit-switch validation while Brian establishes Silo and Erica establishes the baseline/data pipeline. Aim to finish the first workday with saved experimental output and the report outline populated with actual status, even if user interrupts remain unavailable.
