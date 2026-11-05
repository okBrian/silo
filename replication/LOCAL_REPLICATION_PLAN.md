# Local replication plan and status

**Project** · CS 6400 PreemptDB replication in Silo  
**Branch** · `erin/local-replication-evidence`  
**Starting revision** · `8395daba617e83e6805268bda793ab0d5359d63b`  
**Status snapshot** · November 4, 2026

## How to use this file in a future chat

Open the Silo workspace on this branch and attach this file or name its path. For example, ask “Implement MAC-01 and MAC-02 from `replication/LOCAL_REPLICATION_PLAN.md` and update their status using the actual results.” Each task below has a specific completion check. Verify the branch, working tree, and current evidence before starting. Preserve teammate work and advance the requested task IDs.

The [original roadmap](IMPLEMENTATION_PLAN.md) and [implementation status](IMPLEMENTATION_STATUS.md) still track the shared I01–I18 and report tasks. This file tracks the Mac workflow and the handoff to a qualified host. Update the shared tracker when the evidence changes the status of an original task.

## Research target

The course requires a full replication. Keep Intel UINTR-driven preemption in the main comparison with **Wait** and **Cooperative**. Keep the Silo port, the independent request driver, and the mixed workload of long Q2 and short New-Order and Payment transactions. Use the paper's Figure 10 experiment as the first final target, then cover the additional experiments required by the course. Confirm that coverage against the final rubric.

Use the Apple M5 Pro Mac to build and run software checks inside an **emulated x86-64 Linux guest with glibc**. This matches the current Silo code and the earlier local workflow. It can establish correctness for the tested software paths. Actual Intel user-interrupt delivery, interrupt-time context preservation, and the full three-policy performance comparison require a qualified Intel Linux host. The authors' published artifact can provide a reference run on that host. Confirm with the instructor before changing the proposed independent Silo deliverable.

[PreemptDB paper](https://www.cs.sfu.ca/~tzwang/preemptdb.pdf) · [Authors' setup and artifact](https://github.com/sfu-dis/preemptdb) · [Experiment contract](docs/experiment-contract.md)

## Current status

| Task | Status | Evidence or dependency |
| --- | --- | --- |
| MAC-01 · Restore local Linux environment | Complete | Reused the running x86-64 Colima/QEMU guest. A fresh Ubuntu 22.04 container with GCC 11.4 and glibc 2.35 verified Silo `8395dab` and pinned Masstree `1150824`, then built all three targets. [Environment and build](results/mac01-mac02/README.md#guest-and-source) |
| MAC-02 · Reproduce the local baseline | Complete · emulated Linux functional validation | Both engine tests, the request driver, 11 experiment-tool tests, and three voluntary ABI builds passed. All eight Wait and Cooperative runs reconciled, with 8,125 measurement commits, matching short arrivals, and 36 Cooperative mid-Q2 handoffs. The 10,000-read default smoke reached zero checkpoints. [Results and raw evidence](results/mac01-mac02/README.md#results) |
| MAC-03 · Prepare the I03 and I12 kit | Ready | The I03 CPUID check ran on an AWS VM and returned `UINTR=0`. A qualified host and a successful delivery test are still pending. |
| MAC-04 · Prepare I13–I16 software checks | Ready for preparation | Voluntary switching and Q2 resumption are validated locally. The asynchronous switch and integrated preemptive scheduler remain open. |
| MAC-05 · Dry-run the I17–I18 workflow | Ready | The MAC-02 Wait and Cooperative batch regenerates its analysis. The final three-policy configuration and measurements remain open. |
| MAC-06 · Qualify a host and execute the replication | Requires host access | PACE's usable UINTR interface is unavailable to the team. The AWS VM result applies to that guest. Verify any bare-metal or academic alternative separately. |

## MAC-01 — Restore the local Linux environment

1. Inspect the existing profile and reuse it if it can be recovered without losing work. Otherwise use a separate x86-64 Linux guest or named Colima/QEMU profile. Ubuntu 22.04 with GCC 11 and glibc 2.35 is the known reference environment.
2. Check that `uname -m` inside the guest reports `x86_64`. Record the guest image, kernel, compiler, glibc, CPU count, memory, and how the source is shared or copied.
3. Use an isolated Linux build copy. Record its Silo revision and initialize Masstree at the repository's pinned commit `1150824e6a10a0384ba4bfed6b916ce62f96315f`. The Mac checkout's submodule was uninitialized at handoff. Follow the existing HTTPS submodule setup if needed. Build from the pinned source and record any compatibility patch and compiler flags.

**Complete when** the guest starts, the toolchain works, and the build copy's source revisions are verified. [Build and submodule notes](pace-baseline/README.md) · [Prior local environment](results/i07-worker-context/README.md)

## MAC-02 — Reproduce the local baseline

1. Build `dbtest`, `tpcc_mixed_test`, and `silo_context_test` with `WORKER_CONTEXTS=1` using the I11 build recipe. Run the engine tests, the standalone request-driver test, and the experiment-tool tests. Use the I07 recipe for the voluntary context ABI test.
2. Run `queued-cooperative-smoke.json` with the existing runner. Analyze the batch and run `check_cooperative_batch.py`. Confirm matching short-request inputs and deadlines, reconciled outcomes, and at least one mid-Q2 handoff in Cooperative.
3. Save manifests, raw outputs, and a short summary of passes, failures, and any deviations. The 64-read interval is a smoke setting. The archived 10,000-read run reached zero checkpoints, so inspect the Q2 read counts before choosing settings for the final comparison.

**Complete when** the tests and paired smoke reconcile on the current revision, with the evidence labeled **emulated Linux functional validation**. [I11 commands and results](results/i11-cooperative/README.md) · [Smoke configuration](experiments/configs/queued-cooperative-smoke.json)

## MAC-03 — Prepare the I03 and I12 kit

1. Package the existing CPUID probe with a Linux runbook that captures the exact instance or machine type, CPU model, topology, affinity, kernel, and output. Run the capability check before changing the host. If it returns `UINTR=0`, save that result and stop the UINTR setup on that host.
2. Prepare a standalone sender/receiver test for one documented interface, either the UINTR driver or the patched kernel. Record which interface the test targets. Include CPU pinning to separate physical cores where available, bounded timeouts, explicit acknowledgments for repeated sends, a receiver computation with a known answer, and cleanup.
3. Compile the test in the guest where its build dependencies are available. Save the build command and output. Prepare a short runbook for the hardware session so the first test needs neither a Silo build nor a benchmark run.

**Complete locally when** the kit is buildable and ready to transfer. Record I12 as **prepared**. Record I12 as **validated** only after the real receiver handler runs and the interrupted computation resumes correctly on a qualified host. [I03 and I12 requirements](IMPLEMENTATION_PLAN.md) · [Driver and kernel options](https://github.com/sfu-dis/preemptdb#step-2-enable-kernel-support-for-user-interrupt)

## MAC-04 — Prepare the I13–I16 software checks

1. For I13, identify the supported register and extended state, the protected-region rules, and the switch transition states. Prepare deterministic tests for state preservation, nested deferral, and repeated pause and resume. Use I12 to confirm the interface before finalizing the low-level switch.
2. For I14–I16, coordinate with the owners in the shared tracker. Reuse the existing request driver and tests for queue publication, batching, retry accounting, and starvation decisions with controlled timing. Keep queue/admission counts, notification counts, switches, and commits separate.

**Complete locally when** the chosen software changes and checks are recorded with their test results. The integrated interrupt path, delivery races, and Q2 progress under load still need the hardware phase. [I08 state and protection audit](results/i08-silo-context/README.md) · [Policy contract](docs/experiment-contract.md)

## MAC-05 — Dry-run validation and experiment collection

1. Prepare clean-build and smoke commands for the eventual Intel host. Use the existing runner and analysis path, and verify it end to end with Wait and Cooperative locally.
2. Prepare the Figure 10-style run plan. Match binary, workload, arrival settings, queues, and CPU placement across the three policies. Vary policy execution order across repetitions. Aim for the paper's 16-worker setup with one physical core per worker on one socket and a separate sender core, subject to the host's topology. Record changes to engine, workload, or host configuration.
3. Save per-request records and run manifests. Verify the report can recover per-type p50, p90, p99, and p99.9, queue delay, throughput, aborts, and incomplete requests. Record sample counts and label smoke estimates accordingly.

**Complete locally when** a saved two-policy batch can regenerate the analysis. The three-policy performance run remains I18. [Runner and analysis guide](experiments/README.md) · [Experiment contract](docs/experiment-contract.md)

## MAC-06 — Qualify the host and run the replication

1. On the actual Intel Linux host, run I03 and then I12. Record the CPU, kernel or driver, and sender/receiver evidence. Treat a successful I12 delivery test as the requirement for advancing to the hardware-dependent validation.
2. Validate I13's asynchronous switch, then show in I14 that an arriving short transaction runs while Q2 is paused and that Q2 resumes with the expected result. Finish the batch and starvation tests in I15–I16 and the integrated checks in I17.
3. Run all three policies on that same host with matched settings. Start with a small pilot, collect the Figure 10-style results, then run the remaining required experiments. Keep published author results, native Silo measurements, and emulated Mac checks separately labeled in the report.

**Complete when** the required correctness and performance evidence is archived and the reported results can be regenerated. If access remains unresolved near the deadline, take the I03 evidence and current implementation status to course staff for a decision on how to meet the full-replication requirement. [Original roadmap](IMPLEMENTATION_PLAN.md)

## Handoff log

For each update, record the date, task ID, owner, branch and commit, actual commands and results, evidence paths, remaining dependency, and next ready task. Keep the status table current. For milestone reporting, use the existing native stock PACE runs and the local I06–I11 evidence with their environment labels. [M01–M03 roadmap](IMPLEMENTATION_PLAN.md) · [Current shared status](IMPLEMENTATION_STATUS.md)

**November 4, 2026 · MAC-01 and MAC-02 · Erin · `erin/local-replication-evidence` at `8395daba617e83e6805268bda793ab0d5359d63b`.** The first container invocation stopped at Git's ownership check. The retry extracted the source under container ownership, verified both revisions, built the three targets with `WORKER_CONTEXTS=1`, and passed the tests and paired smoke. The separate default-interval run measured 843–2,363 eligible reads per Q2 and zero checkpoints across 713 Q2s. MAC-05 can proceed from the saved batch. Hardware-dependent validation still requires a qualified Intel host and UINTR delivery. [Commands and evidence](results/mac01-mac02/README.md)
