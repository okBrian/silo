# Silo / PreemptDB implementation roadmap

**Purpose:** a sequence of independently requestable implementation tasks for Erin Tan, Brian Ok, and Erica Tay. Use the task IDs below when asking Codex to implement something later. This covers preparation, components, integration, experiments, the milestone report, and the final report.

**Current state as of November 4, 2026**

- **I01 is complete for the current workspace.** This directory contains the shared documentation, provenance, reproducible PACE job, and evidence layout. The original LaTeX project remains an input for M02. **I02 is complete as a source and contract audit.** The pinned reference, mechanism map, Q2 discrepancies and shared interfaces are recorded in [source map](docs/source-map.md) and [experiment contract](docs/experiment-contract.md). Runtime implementation remains in I06–I16.
- **I03 is queued for a hardware probe.** The stock baseline ran on AMD EPYC 7513. Job `6791940` identified the unlabeled Intel CPU node as Xeon Gold 6226. The one-GPU request `6792039` on `ice-gpu` targets Xeon Platinum 8462Y+ and passed submission. Use `squeue -j 6792039` to check the pending reason. CPUID and interface access remain unverified.
- **I04 is validated on PACE.** Job `6791465` produced all ten stock TPC-C measurements. Job `6791730` passed the invariant suite in 5:26 after the pinning test was adapted to use CPUs assigned by Slurm. The original test run selected CPUs starting at CPU 0, outside the benchmark job's allowed range of 54–57. The test-only retry selected CPUs 60–63 from its own allocation.
- **I05 is complete.** The [experiment runner and analysis](experiments/README.md) reproduce the ten-run stock table and process the archived I06 smoke results. At two warehouses, throughput averaged 84,254 txn/s with one worker and 148,330 txn/s with two workers over five 30-second runs each.
- **I06 has local functional evidence.** The independent producer, bounded queues, retry accounting and timing are implemented. The opt-in TPC-C bridge exercises New-Order and Payment. See the [I06 validation and reproduction commands](results/i06-request-driver/README.md).
- **I07 is locally validated on emulated Linux x86-64.** The standalone worker context pair passed one million round trips with separate stack locals and the tested SysV preserved state. See the [I07 validation](results/i07-worker-context/README.md).
- **I08 is locally validated on emulated Linux x86-64.** The [Silo audit and engine test](results/i08-silo-context/README.md) cover voluntary snapshot handoffs, shared epoch protection and deferred work.
- **I09 is locally validated on emulated Linux x86-64.** The [workload and evidence](results/i09-mixed-workload/README.md) cover prepared short inputs, the corrected Q2 variant and one- and two-worker mixed smokes.
- **I10 is locally validated on emulated Linux x86-64.** Select Wait explicitly with `--queued-policy wait`. The [I10 evidence](results/i10-wait/README.md) covers dispatch order, short-request waiting and four mixed-workload runs through I05.
- **M01 has stock baseline and component evidence.** PACE logs and environment records are in [results/pace-runs](results/pace-runs/), and the benchmark CSV and analysis are in `results/i04-pace-baseline/`. The milestone report, I11–I18 and F01–F02 remain open.

The milestone is the immediate deadline and the final date remains to be confirmed. Ownership below is proposed coordination responsibility.

**Silo checkout inspected on November 2, 2026**

- The supplied checkout is clean on local `master` at `cc11ca1ea949ef266ee12a9b1c310392519d9e3b`, with `origin` pointing to Brian's `okBrian/silo` fork. Use this revision as the starting baseline and do development on a separate branch. I01 can skip obtaining a new Silo checkout. Keep the shared experiment and report files in the project workspace.
- The checkout records Masstree at `1150824e6a10a0384ba4bfed6b916ce62f96315f`, but the submodule is uninitialized. Its configured URL uses `git://`. Initialize the pinned commit explicitly on the build copy, using an HTTPS override if necessary, and verify its SHA before building. The Makefile can invoke Git during parsing, including a checkout and pull in its Masstree update path. Inspect it without invoking Make on the original checkout and control submodule setup before running the build on Linux x86-64.
- The existing entry point is `benchmarks/dbtest.cc` with `--bench tpcc`. `benchmarks/tpcc.cc` already implements New-Order, Payment, loading, and warehouse handling. `benchmarks/bench.cc` owns the benchmark loop and results. The inspected checkout has no identified Q2 variant or project scheduler. The completed I02 [source map](docs/source-map.md) covers the reference Q2 variant and its minimum-selection defect. The contract selects the documented `q2_min_stock_v1` correction for I09.
- The stock worker picks its next transaction in `bench_worker::run()` and starts its timer immediately before invoking the transaction. Its summary has aggregate throughput, latency, and abort fields. Use that output for the stock baseline. The new queued driver needs its own per-request timing from arrival through completion, with a stable identity across retries.
- `benchmarks/bench.h` stores the transaction object buffer, string arena, and RNG on the worker. `benchmarks/tpcc.cc` passes the buffer and arena to `new_txn()`. Before running two transactions on one worker, audit all live state and lifetimes, including per-core RCU guards and reclamation in `rcu.h` and `rcu.cc`. A second stack alone leaves these shared objects in place.

**How to use this roadmap**

Say: **“Implement I01 from IMPLEMENTATION_PLAN.md.”** For a sequence, say: **“Implement I01 and I02, then prepare I03.”** For reporting, say: **“Complete M01–M03 using the evidence currently available.”** Each request authorizes the named work and its routine verification; inspect existing work before making changes. If a prerequisite is missing, explain it and finish the independent parts of the requested task. Do not automatically implement unrelated later stages.

At the end of each task, update `IMPLEMENTATION_STATUS.md` with what changed, where the evidence is, tests actually run, limitations, and the next task that is ready. Distinguish **prepared**, **implemented but unverified**, **validated**, and **blocked**. A prepared PACE script is not a successful remote experiment. A reference implementation's test result is not a test result for the Silo port.

The paths below are proposed outputs, not existing files or fixed architectural requirements. Inspect the actual Silo and PreemptDB sources before choosing internal interfaces. Keep source changes in a versioned Silo checkout; keep experiment tools and reports in the surrounding project workspace. Reuse the proposal's actual LaTeX template once its source is available.

**Research scope and rules**

- Primary final target: a Figure 10-style comparison of Wait, Cooperative, and UINTR-driven Preemptive scheduling in Silo, with New-Order and Q2 latency and supporting throughput/abort measurements.
- Preserve transaction correctness and report engine differences. An improvement is a hypothesis; negative results are valid.
- Published PreemptDB runs are a separate reference track. They do not replace the proposed Silo implementation without an explicit scope decision.
- Preserve attribution and licenses for reused code. Record upstream revisions and local modifications.
- Never fill a results table with expected numbers. Clearly label fixtures, simulated clocks, synthetic workloads, missing measurements, and small-sample estimates.
- Actual PACE runs require an accessible allocation/session. Privileged kernel/driver configuration requires an appropriate approved machine; preparation can proceed without it.
- Do not purchase compute, send support messages, or submit coursework as part of a coding task unless separately requested.

**Execution order**

For the milestone, use the completed **I04** and **I05** stock runs for **M01**. Qualify an Intel candidate in **I03** while finishing the relevant **I02** request contract. **I06** and **I07** can supply component evidence independently of UINTR access. Finish with **M02 → M03** using the evidence available before submission.

For the final, continue through **I08–I18**, then **F01–F02**. Dependency references within each task take precedence over the numbering; independent tasks may proceed separately. “Parallel” here describes team work, not permission to spawn software agents automatically.

**I01 — Establish the workspace and evidence conventions**

Owner: Brian, with everyone reviewing. Prerequisites: none.

Steps:
1. Inspect existing files and any repository instructions; preserve prior work.
2. Set up directories for code checkouts, scripts, configurations, tests, results, figures, documentation, and report sources. Keep generated binaries and large raw results out of ordinary source commits.
3. Add a project README, task status file, result metadata format, and source-attribution log.
4. Separate immutable upstream snapshots/revisions from the working Silo changes. Do not accidentally create nested repository tracking problems.
5. Record missing inputs: original LaTeX source, remote compute access, existing teammate code, final deadline.

Outputs: workspace layout, `README.md`, `IMPLEMENTATION_STATUS.md`, and `docs/provenance.md`.

Complete when: a teammate can identify where code, tests, raw results, and report source belong. No benchmark success is claimed.

Request: **“Implement I01: set up the project workspace and progress tracking.”**

**I02 — Inspect and pin the sources; define the experiment contract**

Owner: everyone, coordinated by Brian. Prerequisites: I01.

Steps:
1. Use the recorded Silo and Masstree revisions above. Inspect and pin the public PreemptDB reference.
2. Map paper mechanisms to relevant source files: context state, switching, handler, protected regions, queues, workload, batching, and starvation accounting.
3. Inspect the actual Q2 variant and request-generation semantics. Record gaps rather than guessing.
4. Define the common request lifecycle, policy interfaces, queue ownership, clock/timestamp conventions, and shutdown behavior.
5. Create a paper-versus-implementation configuration table, including known deviations and unknowns. Correct the proposal's 88–96% comparator to Wait.

Outputs: `docs/source-map.md`, `docs/experiment-contract.md`, and pinned source provenance.

Completed November 3, 2026. See the [audit evidence](results/i02-source-audit/README.md) for inspected revisions, verification and remaining runtime gates.

Complete when: later components have compatible interfaces and every important configuration value has a source or is explicitly a project choice. No uninspected internal filename is treated as fact.

Request: **“Implement I02: inspect the sources and write the implementation and experiment contracts.”**

**I03 — Prepare and execute machine qualification**

Owner: Erin. Prerequisites: I01; source/setup details from I02 where needed.

The archived baseline and test jobs qualify the AMD EPYC 7513 node for stock Silo. For I03, the target is an approved Linux x86-64 machine with Intel UINTR exposed to the workload and an available kernel interface or driver. PreemptDB documents both options. [PreemptDB setup](https://github.com/sfu-dis/preemptdb#step-2-enable-kernel-support-for-user-interrupt) · [UINTR driver](https://github.com/UB-ADBLAB/uintr-driver)

The `coc-cpu` listing labels most Intel nodes `gold6226`. Intel identifies that model as Cascade Lake, and job `6791940` confirmed the same CPU on the unlabeled host `atl1-1-03-004-5-2`. The broader Slurm listing showed `platinum8462y+` on H100 nodes in `ice-gpu` and `coe-gpu`. The 8462Y+ is a Sapphire Rapids candidate. Partition visibility alone does not establish allocation access. Check the account association and partition limits before probing it. [Xeon Gold 6226 specifications](https://www.intel.com/content/www/us/en/products/sku/193957/intel-xeon-gold-6226-processor-19-25m-cache-2-70-ghz/specifications.html) · [Intel Sapphire Rapids models](https://www.intel.com/content/www/us/en/ark/products/codename/126212/products-formerly-sapphire-rapids.html) · [Intel UINTR documentation](https://www.intel.com/content/www/us/en/support/articles/000102531.html)

1. From the PACE login shell, inspect the node features available in the partition used for the baseline.

   ```bash
   sinfo -N -p coc-cpu -o '%N %t %f'
   sinfo -N -h -o '%P %N %t %f' |
     grep -Ei '8462y|6448h|sapphire|spr|sierra|uintr'
   ```

   For the identified candidate partitions, check your account and QoS association and the partition access settings from the login shell.

   ```bash
   sacctmgr -nP show assoc where user="$USER" format=Account,Partition,DefaultQOS,QOS
   for p in ice-gpu coe-gpu; do
     scontrol -o show partition "$p" | tr ' ' '\n' |
       grep -Ei '^(PartitionName|AllowAccounts|DenyAccounts|AllowGroups|AllowQos|DenyQos|QoS)='
   done
   ```

   Ask PACE staff which account, partition, and QoS permit a brief CPU-only probe on an 8462Y+ node if the allocation route is unclear or the partition requires a GPU. Verify the allocated processor on the node. [Slurm association fields](https://slurm.schedmd.com/sacctmgr.html) · [Slurm partition access fields](https://slurm.schedmd.com/scontrol.html)

2. The reported association is `coc` with `coc-ice`. The reported `ice-gpu` partition allows all accounts and QoS values, while `coe-gpu` lists `ice-pace-staff`, `coe-ice`, and `coe-grade`. The CPU-only `ice-gpu` request was rejected with `Partition ice-gpu requires use of GPUs`. If your course permits this brief diagnostic on the GPU partition, request one GPU and target the candidate CPU feature. Either `srun` or `sbatch` accepts `--gres=gpu:1`. [Slurm GPU requests](https://slurm.schedmd.com/gres.html)

   ```bash
   srun -A coc -p ice-gpu --qos=coc-ice \
     --constraint='platinum8462y+' --gres=gpu:1 \
     -N 1 -n 1 -c 1 --mem=1G -t 00:05:00 --pty bash
   ```

   Run the CPU probe inside that shell and exit when finished. If the GPU request is rejected or your course has no allocation for it, ask PACE for the appropriate account and QoS or a suitable CPU partition. Ask separately whether a compatible UINTR kernel or driver is available to student jobs. [PACE user support](https://pace.gatech.edu/user-support/)

3. Inside that allocation, capture the CPU model, kernel, allocation, and hints about any exposed UINTR interface. Use a unique log per job.

   ```bash
   mkdir -p "$HOME/silo-i03"
   probe_log="$HOME/silo-i03/pace-${SLURM_JOB_ID}.log"
   {
     hostname
     uname -r
     lscpu | grep -E '^(Architecture|Vendor ID|Model name|Flags):'
     grep -E '^(Cpus_allowed_list|Mems_allowed_list):' /proc/self/status
     grep -i uintr /proc/modules || true
     find /dev -maxdepth 1 -iname '*uintr*' -ls
   } 2>&1 | tee "$probe_log"
   ```

4. On the Intel candidate, directly check the CPUID feature bit. Linux flag listings and module/device listings are useful hints. CPUID verifies hardware exposure, and the system interface still needs confirmation. This minimal check uses the same UINTR bit as the authors' checker. It reads CPUID without executing an interrupt instruction. [Authors' CPU checker source](https://github.com/UB-ADBLAB/check_uintr/blob/main/check_uintr.c)

   ```bash
   probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/silo-uintr.XXXXXX")
   cat > "$probe_dir/cpuid.c" <<'EOF'
   #include <cpuid.h>
   #include <stdio.h>

   int main(void) {
       unsigned int eax, ebx, ecx, edx;
       if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
           puts("CPUID leaf 7 unavailable");
           return 1;
       }
       printf("CPUID.7.0.EDX=0x%08x UINTR=%u\n", edx, (edx >> 5) & 1u);
       return 0;
   }
   EOF
   gcc -O2 -Wall -Wextra "$probe_dir/cpuid.c" -o "$probe_dir/cpuid" &&
     "$probe_dir/cpuid" | tee -a "$probe_log"
   ```

5. Record the hardware and system decisions separately.

   - AMD host or `UINTR=0` on the Intel candidate. Classify that node as unsupported for the target and look for another node.
   - Intel and `UINTR=1`. Record CPU support and ask PACE staff whether the running kernel exposes a compatible interface or whether an approved driver is already loaded and accessible to your jobs. If staff cannot provide either, pursue an approved lab machine or verified dedicated host. Kernel and driver setup are administrative work on shared PACE systems.
   - Record the approved route, node/feature, kernel, probe log, and any remaining access constraint. If the interface remains unconfirmed, document that state and the fallback in the milestone. **I12** verifies actual sender/receiver delivery once an interface is available.

Outputs are the probe log, the system-access decision, and the fallback if needed. The archived AMD baseline is the first I03 finding. An Intel probe and the access decision are pending.

Request: **“Implement I03 using the archived baseline environment and the candidate Intel PACE allocation.”**

**I04 — Build and validate the stock Silo baseline**

Owner: Brian. Prerequisites: I02; suitable Linux compute access for runtime validation.

Steps:
1. Prepare a Linux x86-64 build copy from the recorded Silo revision. Explicitly initialize pinned Masstree and check the toolchain and libraries in `BUILD` and `Makefile`, including NUMA for the core build and the additional benchmark libraries for `dbtest`. Record any dependency or compatibility changes.
2. Add reproducible debug/invariant and performance build commands based on the pinned source.
3. Run the supplied correctness suite and a tiny benchmark smoke test.
4. Save build logs, test output, versions, exit status, and any compatibility patches.
5. Keep the original baseline runnable after later changes.

Outputs: build/test scripts, baseline manifest, logs, and minimal documented compatibility fixes if necessary.

Complete when: upstream tests and the benchmark smoke test pass on the recorded environment. If runtime access is missing, mark preparation separately from validation.

Request: **“Implement I04: build and validate the unchanged Silo baseline.”**

**I05 — Implement the experiment runner and result analysis**

Owner: Erica. Prerequisites: I02; I04 to collect actual Silo results.

Steps:
1. Implement configuration-driven runs with unique run IDs, recorded commands, seeds, revisions, CPU placement, warm-up, measurement duration, and exit status.
2. Preserve raw logs. Parse the five-column stock `dbtest` summary using the order defined in `benchmarks/bench.cc`. Label the stock latency as the reported average of timed successful attempts. Add per-request queue delay and end-to-end latency from the new driver separately.
3. Add run-summary tables and plots; use known small fixtures to verify parsing, units, and percentile calculations where samples exist.
4. Support repeated one- and two-worker stock-baseline runs with an explicitly chosen small dataset.
5. Record failures and missing metrics rather than silently dropping runs or replacing missing values with zero.

Outputs: runner, parser, configuration files, analysis scripts, baseline result table, and reproducible plot commands.

Completed November 3, 2026. See the [regenerated evidence](results/i05-experiments/README.md) and [commands](experiments/README.md).

Complete when: raw logs reproduce the summary, fixture calculations agree with known values, and any reported baseline numbers come from real completed runs.

Request: **“Implement I05: build the experiment runner, collect small Silo baselines, and generate the results table.”**

**I06 — Implement the independent request driver and timing**

Owner: Erica. Prerequisites: I02; use I05's result format. Initial tests need no database or UINTR.

Steps:
1. Implement requests with stable IDs, type/inputs, generation timestamp, execution timestamps, completion outcome, and retry count.
2. Generate arrivals independently of worker completion, with explicit batch size, interval, queue capacity, and overload behavior.
3. Implement queue ownership and synchronization appropriate to the actual producer/consumer pattern. Keep an explicit opt-in path for queued experiments at the `bench_worker::run()` dispatch boundary so existing stock benchmark invocations still use the original loop.
4. Add deterministic tests for waiting-time accounting, queue-full handling, duplicate/lost requests, retries, shutdown, and draining.
5. Run a controlled slow-worker experiment showing that a queued short request's end-to-end latency includes waiting.

Outputs: reusable driver component, tests, and saved functional-test output.

Implemented in `benchmarks/request_driver.{h,cc}` and wired into `bench_worker::run()` behind `dbtest --queued RUN_ID`. The standalone suite exercises low-priority traffic with a synthetic callback. The TPC-C bridge currently runs New-Order and Payment with one warehouse per worker. I09 supplies the Q2 workload and explicit input materialization. See the [I06 evidence](results/i06-request-driver/README.md).

Complete when: generated requests reconcile with completed, outstanding, rejected, and dropped requests; timestamps are valid; retries retain the original logical request identity. Report generation delay separately if the generator misses its intended arrival schedule.

Request: **“Implement I06: build and validate the independent request generator, queues, and timing.”**

**I07 — Implement explicit two-context switching**

Owner: Erin. Prerequisites: I02 and a suitable execution environment. No UINTR dependency.

Steps:
1. Define context lifecycle, stack allocation/ownership, initialization, switching, and cleanup.
2. Implement or clearly attribute a voluntary switching primitive suitable for later Silo integration.
3. Verify the applicable ABI requirements, stack alignment, and preservation of required state.
4. Test alternating contexts with distinct local values, known computations, and repeated switching; increase the count after small tests pass.
5. Document what voluntary-switch validation does not establish about asynchronous interrupt-time state preservation.

Outputs: context module, focused test harness, and a context-validation table.

Completed November 3, 2026 for voluntary switching on emulated Linux x86-64 with glibc. The worker's original stack and one guarded alternate stack exchange control through `resume()` and `yield()`. The [validation record](results/i07-worker-context/README.md) covers the lifecycle, ABI checks and reproduction commands. I08 owns transaction-state and reclamation integration. I13 owns interrupt-time register preservation.

Complete when: recorded tests preserve expected values and context isolation over repeated round trips. Supported architectural/compiler assumptions are explicit.

Request: **“Implement I07: build and validate explicit switching between two userspace contexts.”**

**I08 — Integrate context-local transaction state and protected regions**

Owner: Brian with Erin. Prerequisites: I04, I07.

Steps:
1. Start with `bench_worker::txn_obj_buf`, `arena`, and `r`, then check TPC-C scratch values, `masstree_btree.h` scan guards, `rcu.h`, `ticker.h`, and the transaction protocol in `txn_proto2_impl.{h,cc}`. Audit state and lifetimes before permitting a yield while a transaction or scan is active.
2. Decide what stays worker-local, what becomes context-local, and what requires coordinated lifetime protection for both live contexts.
3. Introduce the transaction control block/context-local state without changing isolation semantics.
4. Implement nested non-preemptible regions around audited unsafe operations and safe transition boundaries. Define how deferred work eventually receives service.
5. Integrate two contexts in a small engine test; exercise commit/abort/error paths and nesting.

Outputs: source audit, Silo integration changes, state-isolation/protection tests, and regression results.

Implemented and locally validated November 3, 2026. The [I08 audit and validation](results/i08-silo-context/README.md) document context-owned transaction storage, nested protection, and the worker epoch shared by a suspended snapshot and a short attempt. `WORKER_CONTEXTS=1` enables the instrumented engine build. I11 owns integration of checkpoint polling with the workload and scheduler. I13–I14 own asynchronous switching.

Complete when: one context cannot overwrite the other's transaction state, shared memory cannot be reclaimed while the paused context needs it, and protected-region nesting remains correct on all tested paths. Merely replacing thread-local variables is insufficient.

Request: **“Implement I08: integrate context-local state and nested non-preemptible regions into Silo.”**

**I09 — Implement and validate the mixed workload**

Owner: Erica with Brian. Prerequisites: I02, I04; I06 for driver integration.

Steps:
1. Adapt the existing `tpcc_worker` New-Order and Payment entry points to execute driver-supplied work while preserving transaction semantics. Verify how request inputs are generated and reused on retries. Complete and test this short-transaction adapter as a distinct checkpoint before porting Q2.
2. Port the reference Q2 variant's required data access and computation into Silo. Document schema/data-generation adaptations.
3. Build tiny deterministic datasets and compare Q2 results with a trusted reference calculation.
4. Test short transaction outcomes and applicable consistency conditions.
5. Define warehouse selection, remote-access probabilities, workload mix, and retries from the inspected sources; label any differences.

Outputs: workload adapters, Q2 implementation, deterministic correctness tests, and workload configuration.

Implemented in the queued TPC-C path with `--queued-mixed`. The [I09 record](results/i09-mixed-workload/README.md) documents the schema projection, input distributions, checksum, tests and commands. I10 uses the same workload for the Wait baseline.

Complete when: workload outputs and relevant consistency checks pass. A synthetic delay is retained only as a driver-test workload and never labeled Q2.

Request: **“Implement I09: add and validate the New-Order, Payment, and Q2 mixed workload.”**

**I10 — Implement the Wait scheduler**

Owner: Erica with Brian. Prerequisites: I06 and I09's short-transaction adapter for the initial Silo smoke test. Q2 is required for the mixed-workload baseline. Basic scheduler tests can use synthetic requests earlier.

Steps:
1. Execute each transaction without voluntary or interrupt-driven switching.
2. At transaction boundaries, choose pending high-priority work according to the agreed reference policy.
3. Preserve common queue, workload, timestamp, retry, and result behavior.
4. Test a short request arriving during a long transaction and verify execution order and waiting time.
5. Collect an initial small mixed-workload baseline through I05.

Outputs: selectable Wait policy, scheduling tests, and measured baseline logs.

Complete when: known schedules produce expected behavior and matched workload results are reproducible.

Wait reuses the queued driver and I09 adapters. Each worker finishes the active request and its retries before selecting admitted short work in FIFO order ahead of pending Q2. The [I10 record](results/i10-wait/README.md) contains the deterministic test, I05 recipe and four reconciled local runs. I11 can use the same workload and measurement definitions.

Request: **“Implement I10: add the Wait baseline and run a small mixed-workload experiment.”**

**I11 — Implement Cooperative scheduling**

Owner: Brian. Prerequisites: I07, I08, I10.

Steps:
1. Add a configurable record-access yield counter at valid audited locations.
2. Use the two contexts to pause Q2, service high-priority work, and resume Q2.
3. Prevent switching inside protected regions and preserve the transaction's live state.
4. Test empty queues, deferred yields, repeated pauses, aborts, and resumption.
5. Run matched Wait/Cooperative comparisons without changing workload inputs or measurement definitions.

Outputs: selectable Cooperative policy, interval configuration, tests, and preliminary comparison.

Complete when: Q2 resumes correctly, transaction correctness checks pass, and the actual yield/switch behavior matches the configured policy. It need not improve latency to pass validation.

Request: **“Implement I11: add and validate Cooperative scheduling against Wait.”**

**I12 — Validate actual user-interrupt delivery**

Owner: Erin. Prerequisites: I03 and an approved working UINTR interface.

Steps:
1. Inspect the selected kernel/driver interface and configure a minimal compatible sender/receiver program.
2. Pin test threads within the allocated CPU set and register, enable, deliver, handle, and clean up interrupts.
3. Verify receiver computation resumes with the expected result.
4. Test repeated delivery using explicit acknowledgments so interrupt coalescing is not mistaken for a one-to-one request guarantee.
5. Save configuration, output, errors, and diagnostic timing separately from database measurements.

Outputs: UINTR smoke test and actual hardware validation record.

Complete when: real interrupts are handled successfully on the recorded host. Capability detection or signal-based emulation does not satisfy this task.

Request: **“Implement I12: build and run the minimal hardware user-interrupt delivery test.”**

**I13 — Implement interrupt-driven context switching**

Owner: Erin with Brian. Prerequisites: I07, I08, I12.

Steps:
1. Implement passive switching from interrupt arrival and active switching back, using the selected ABI/interface correctly.
2. Preserve all state required at an arbitrary interruption point, including supported floating-point/vector state where relevant; explicitly constrain unsupported compilation modes rather than ignoring them.
3. Make interrupted transitions and non-preemptible regions safe; avoid unsafe allocation/logging operations in the handler.
4. Add asynchronous interruption tests during known computations and around protected-region boundaries.
5. Verify eventual handling of deferred work and correct continuation of the interrupted computation.

Outputs: interrupt/context integration, architecture assumptions, and asynchronous preservation tests.

Complete when: deterministic expected computations survive repeated real interrupts; protection and transition tests pass. Success of I07 alone is not sufficient.

Request: **“Implement I13: add and validate interrupt-driven context switching.”**

**I14 — Integrate the Preemptive scheduler into Silo**

Owner: Brian and Erin. Prerequisites: I08, I10, I13.

Steps:
1. Connect queue admission, worker notification, interrupt delivery, context execution, and return to the paused transaction.
2. Keep the same workload and measurement interfaces as Wait and Cooperative.
3. Test single-worker scheduling first, then multiple workers and concurrency/abort paths.
4. Verify the supported two-priority behavior: do not recursively preempt the currently executing high-priority transaction.
5. Add diagnostics for notification attempts, handled/deferred interrupts, switches, and completed work without conflating their counts.

Outputs: selectable Preemptive policy and integrated correctness evidence.

Complete when: an arriving short transaction executes before a long transaction finishes, and the long transaction resumes correctly with valid database state. This stage does not yet establish fairness under overload.

Request: **“Implement I14: integrate and validate Preemptive scheduling in Silo.”**

**I15 — Implement batched on-demand preemption**

Owner: Brian. Prerequisites: I14.

Steps:
1. Implement reference-compatible batch admission and worker selection with configurable queue capacities.
2. Notify a worker for admitted work according to the inspected batching protocol, avoiding unnecessary per-request interrupts.
3. Resolve races between admission, queue draining, return to Q2, and notification so queued work cannot be stranded.
4. Test partial batches, full queues, multiple workers, delayed delivery, and empty notifications.
5. Measure work completion and notification/switch overhead across small batch configurations.

Outputs: batching policy, race/queue tests, and batch-accounting diagnostics.

Complete when: request accounting reconciles and all admitted work is eventually serviced under the tested load; interrupt counts are interpreted according to the actual delivery semantics.

Request: **“Implement I15: add and validate batched on-demand preemption.”**

**I16 — Implement starvation prevention**

Owner: Brian with Erin reviewing timing. Prerequisites: I15.

Steps:
1. Map the paper/reference accounting window and threshold units to explicit code and configuration.
2. Track high-priority execution time relative to the current low-priority transaction's elapsed execution interval, with correct reset behavior.
3. Apply admission and return-to-low-priority decisions at the inspected policy decision points.
4. Test thresholds, timing arithmetic, retries, long high-priority requests, and sustained overload.
5. Report both low-priority CPU service and committed progress; receiving CPU does not guarantee a repeatedly aborting Q2 will commit.

Outputs: fixed-threshold policy, accounting tests, and fairness experiment logs.

Complete when: the implemented decisions match the defined policy, low-priority work receives service under the tested overload, and measured overshoot is explained. Do not claim a strict instantaneous CPU cap when decisions occur only at transaction boundaries.

Request: **“Implement I16: add and validate starvation prevention.”**

**I17 — Validate the integrated system and reproducibility**

Owner: everyone. Prerequisites: I11, I16.

Steps:
1. Run the relevant upstream tests and new component/integration tests against the current code revision.
2. Exercise no-contention and conflicting transactions, context-state isolation, reclamation/lifetime cases, queue pressure, retries, shutdown, and long-running stress.
3. Check applicable database consistency invariants before/after workloads and capture failures with reproducible seeds/configurations.
4. Verify one common measurement implementation and equivalent configuration across policies.
5. Re-run selected tests from a clean checkout using the documented workflow.

Outputs: validation summary, known limitations, and a stable experiment candidate revision.

Complete when: required checks pass or a clearly identified correctness blocker prevents performance claims. Stress tests are supporting evidence, not a proof of correctness.

Request: **“Implement I17: validate the complete scheduler and freeze a reproducible experiment candidate.”**

**I18 — Run the main replication and limited sensitivity experiments**

Owner: Erica, with everyone reviewing. Prerequisites: I05, I09, I17 and qualified compute access.

Steps:
1. Freeze the main configuration and document differences from the paper. Start with a small matched pilot before the target configuration.
2. Run Wait, Cooperative, and Preemptive with matched inputs/settings, repetitions, and varied execution order.
3. Report per-type latency percentiles, committed throughput, aborts/retries, offered/completed rates, sample counts, and uncompleted work.
4. Produce the Figure 10-style plot and supporting tables. Check percentile uncertainty and explain whether summaries pool samples or summarize independent runs.
5. After the main result, run a limited sensitivity study of yield interval, batch size, or starvation threshold as time allows.
6. Investigate discrepancies through targeted experiments; retain negative and failed runs in the record.

Outputs: raw experiment archive, configuration matrix, plots, tables, and discrepancy analysis.

Complete when: figures can be regenerated from raw data and claims distinguish within-Silo comparisons from published cross-engine references. Improved numbers are not a completion requirement.

Request: **“Implement I18: run the matched replication experiments and generate the analysis.”**

**M01 — Freeze milestone evidence and assess progress**

Owner: everyone. Prerequisites: any actually completed tasks; does not require I08–I18.

Steps:
1. Inventory actual implementation, tests, and measurements; link every result to a source revision and log.
2. Select at least one substantive component-validation result and, if available, the stock baseline measurements.
3. Generate small readable tables/figures using real outputs only.
4. Compare actual progress with the original proposal's milestone commitments.
5. Record remaining tasks, two principal risks, hardware findings, and a dated recovery plan.

Outputs: milestone evidence manifest, figures/tables, and actual progress assessment.

Complete when: every proposed report claim has supporting evidence or is explicitly future work. A lack of sufficient evidence is surfaced rather than concealed.

Request: **“Complete M01: assemble and assess the milestone evidence we actually have.”**

**M02 — Write the milestone in the proposal's LaTeX template**

Owner: Erica assembling; Brian and Erin author their findings. Prerequisites: M01 for results; original LaTeX project for exact template reuse. Outline work can start earlier.

Steps:
1. Preserve the template, author list, bibliography setup, and relevant proposal structure.
2. Write implementation status, division of work, timeline assessment, and remaining work.
3. Present experimental methods, actual results, validation, and limitations.
4. Update the Replication Plan with clearly marked changes and reasons.
5. Answer proposal feedback in 1–2 sentences based on actual machine-access progress.
6. Describe resolved risks only where supported, and at most two remaining risks with concrete mitigation triggers.

Outputs: milestone `.tex`, bibliography/assets, and draft PDF when buildable.

Complete when: all assignment requirements are addressed without fabricated results or unsupported completion claims. If template source is unavailable, prepare reusable section text and identify exact template reuse as pending.

Request: **“Complete M02: write the milestone report using our template and verified evidence.”**

**M03 — Verify and package the milestone**

Owner: everyone. Prerequisites: M02.

Steps:
1. Compile and resolve missing citations/assets and significant layout errors.
2. Render and inspect the PDF, including figure labels, table readability, and page layout.
3. Trace reported numbers to raw results and check status/ownership statements against the evidence manifest.
4. Verify change marking, feedback response, risk count, and final checklist.
5. Package the final PDF and editable source; identify the group submitter and actual deadline in the handoff.

Outputs: submission-ready milestone PDF and reproducible source package. Canvas submission remains a separate user action unless explicitly requested.

Complete when: the report is both visually reviewed and fact-checked.

Request: **“Complete M03: check and package the milestone PDF for submission.”**

**F01 — Write the final report**

Owner: everyone, Erica assembling. Prerequisites: I18 or the documented reduced-scope evidence, plus the final-report assignment requirements when available.

Steps:
1. Read the final rubric; do not assume it is identical to the milestone rubric.
2. Explain the implementation, experiment setup, deviations, results, correctness evidence, and reproducibility.
3. Compare findings with the paper and discuss discrepancies, including engine/concurrency-control effects and measurement limitations.
4. Separate achieved replication, unsupported claims, incomplete goals, and optional extensions.
5. Integrate citations, figures, and actual contributions in the required template.

Outputs: final report source and draft PDF.

Complete when: every central claim is supported and the final rubric is covered.

Request: **“Complete F01: write the final report from our replication results and the final rubric.”**

**F02 — Final verification and handoff**

Owner: everyone. Prerequisites: F01.

Steps:
1. Rebuild the report and regenerate selected tables/plots from saved raw data.
2. Check numerical claims, citations, code attribution, limitations, and task status.
3. Render and visually inspect the final PDF.
4. Package source, configurations, run/analysis instructions, and an evidence index; omit credentials and unnecessary large build artifacts.
5. Deliver the PDF and a concise reproducibility guide for the group.

Outputs: final report PDF, source package, and reproduction instructions.

Complete when: the project can be understood and its reported analysis reproduced from the delivered materials.

Request: **“Complete F02: verify and package the final report and reproducibility materials.”**

**Optional extensions — request separately after the main result**

- **X01: adaptive starvation threshold.** Define the controller, compare with a fixed-threshold baseline, and evaluate stability as well as latency/fairness.
- **X02: portability study.** First determine which comparable notification mechanism is supported on the proposed architecture. Separate architecture effects from changed interrupt mechanisms; do not assume Intel UINTR is portable unchanged.
- **X03: distributed notification.** Define network request semantics, worker-local interrupt delivery, failure handling, and end-to-end timing before implementing. A network broadcast is not itself a userspace interrupt across machines.

These are new studies, not dependencies of the proposed core replication or milestone.

**Reference sources already identified**

- [PreemptDB paper](https://www.cs.sfu.ca/~tzwang/preemptdb.pdf)
- [PreemptDB implementation](https://github.com/sfu-dis/preemptdb)
- [Silo implementation and build instructions](https://github.com/stephentu/silo)
- [CPU capability checker](https://github.com/UB-ADBLAB/check_uintr)
- [Alternative UINTR driver](https://github.com/UB-ADBLAB/uintr-driver)
- [Patched-kernel repository](https://github.com/UB-ADBLAB/ubuntu-linux-uintr)

Refresh the relevant source/interface details at implementation time. The accompanying `research-and-milestone-plan.md` explains the two-day schedule and report requirements; this file is the canonical task-ID roadmap.
