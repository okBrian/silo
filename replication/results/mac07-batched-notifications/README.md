# MAC-07 controlled batched notifications

November 5, 2026 · **Local software validation; checkpoint service** ·
`erin/local-replication-evidence`, source base
`bd557b2c358af7886ba1785c2022e5614b7ed741` plus the
[source changes](source-changes.tar.gz). The pre-existing local-plan edits were
preserved. This task does not implement MAC-09's signal backend.

## Implementation

[NotificationModel](../../../benchmarks/notification_model.h) separates send
attempts, pending/coalesced events and controlled deliveries from queue admission.
It uses one atomic pending bit per worker; the caller chooses delivery time and
can inject unsolicited duplicate/spurious events. Delivery invokes an ordinary
test-thread callback. It is not a signal handler or an Intel/kernel emulator.

The [driver](../../../benchmarks/request_driver.h) adds the explicitly named
`Policy::notification_checkpoint` C++ test backend, notification/scheduling
counters and worker-side transition hooks. It reuses the existing fixed-routing
queues, producer publication callback, low context and high-service loop. It
services work only at audited record-read checkpoints or transaction boundaries.
The queue is authoritative even when a hint is delayed, coalesced, consumed under
protection or stale. Clearing a hint before checking the queue, then checking
the queue again at subsequent reads, keeps work published at the return transition
eligible for service. Idle publication wakes remain independent of this transport.
The benchmark CLI still exposes Wait and Cooperative; this test backend is not
advertised as Preemptive.

[Shared cases](../../tests/batched_notification_cases.h) run through both the
[standalone harness](../../tests/batched_notifications_test.cc) and the
[Silo Q2 fixture](../../tests/tpcc_mixed_test.cc). Each transition case checks
unique contiguous request IDs, terminal outcomes, timestamps, exactly four high
commits, one scripted conflict/retry, two protected deferrals, the expected switch
count, and exact low computation/checksum continuation. The Silo fixture uses
real Q2 snapshots and four committed stock writes while the active Q2 is paused.
Its conflict is injected before a write, rather than a measured Silo conflict.
Queued subsequent low requests also execute the supplied low computation; their
checksums are recorded separately. The existing engine suite covers actual
short-transaction rollback and retry.

## Reproducible schedules

All new schedules use seed **1** and a controlled logical nanosecond clock.
The time values are synchronization inputs, not latency measurements.

| Case | Schedule and oracle |
| --- | --- |
| Transition matrix | At 100, start low and consume a spurious hint against an empty queue. At 110, publish two shorts. Try service under two nested protection levels; neither switches. At the first safe read, pause low. Publish the next two shorts at 120, precisely at `high_entry`, after a failed high pop (`high_empty`), or immediately before low resumption. Advance to cutoff 130 after the active low completes; drain allowance is 1,000. |
| Ordinary delivery (mode 0) | Two publication callbacks, two sends, three modeled deliveries including the initial spurious event. |
| Duplicate delivery (mode 1) | Two sends, five deliveries including initial and duplicate unsolicited events; still exactly four high commits. |
| Delayed/coalesced delivery (mode 2) | Two sends per callback, withheld until the selected transition: four sends, three coalesced events, two deliveries including the initial spurious event. Queue fallback permits the first handoff before transport delivery. |
| Two-worker partial admission | Batch size six, global capacity four, worker capacity two. Hold both active lows while batches arrive at 110 and 120. At the 130 admission pass, refill the queues after each worker has popped its original two shorts. Exactly 12 high arrivals, eight admissions/commits, four global queue-full rejections, four callbacks, eight modeled sends, six coalesced events, four deliveries, and nine high attempts. Wait dispatch makes zero mid-low switches. |
| Drain expiry | One worker, batch/global capacity four, worker capacity one, cutoff 120 and deadline 130. Hold the active low until 130. One admitted high, three global highs, and one queued low are cancelled; the active low completes safely at the deadline. Deliver the stale hint after worker join, while the driver still lives; no additional work executes. |

For every nine-case transition matrix, the expected totals are 36 high commits,
45 scripted high attempts, 18 publication callbacks, 24 modeled sends, nine
coalesced events, 30 modeled deliveries, 18 protected deferrals and 15 low-to-high
handoffs followed by 15 low resumptions. An entry-time publication drains in one
handoff; empty-check and return-time publications take two. All matrix transport
and scheduler pending flags clear. Real signal/UINTR handler entries are **zero**.
Counter lines and raw terminal records are saved in each run log.

## Environment and commands

The [host record](host-environment.log) identifies an Apple M5 Pro MacBook Pro
(48 GB), macOS 27.0.1, arm64, and Apple Clang 21.0.0. The Linux validation uses
the existing Colima/QEMU x86-64 guest, a separate container with an idle `sleep`
entrypoint and no host mounts. The [container identity](container.log), guest
environment, source hashes and artifact hashes are saved alongside the logs.
The container image was committed from the stopped MAC-01/MAC-02 toolchain
container; the historical container was not restarted. Compilation takes place
in a newly extracted source directory with no prior object files.

The source snapshot was `git archive` of the source base with the seven changed/new
source and validation files overlaid. A local detached-copy Git directory was
supplied for experiment-runner metadata. Masstree was cloned from the saved guest
cache and verified at `1150824e6a10a0384ba4bfed6b916ce62f96315f`.
When supplying `.git` from a `--no-checkout` clone, populate its index with
`git reset --mixed HEAD` in the disposable copy before validation; this preserves
the overlaid working files.

The final guest validation command was:

```bash
cd /mac07-input/source
bash replication/tests/validate_batched_notifications.sh /mac07-evidence
```

[The script](../../tests/validate_batched_notifications.sh) records exact compiler
commands, checks x86-64 Linux and Git metadata, runs standalone debug (`-O0 -g`),
optimized (`-O3`, assertions retained) and UBSan (`-O2`, no recovery) builds,
20 additional optimized repetitions, existing Wait/Cooperative driver suites and
experiment-tool tests. It then applies the archived MAC-01 GCC formatter/`-O1`
compatibility patch only in the disposable source copy, builds all three engine
targets with `WORKER_CONTEXTS=1`, and runs both engine tests. Individual standalone
runs have a 60-second timeout; each engine test has a 120-second timeout.

On macOS, compiled the existing driver suite with
`-std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread`; compiled the new
standalone harness with the same flags and without contexts. Also ran the new
Wait-build harness with `-O1 -g -fsanitize=thread`, and the 13 Python experiment
tests with `python3 -B -m unittest discover -s replication/tests -p
'test_i05_experiments.py' -v`. Logs are retained here. Full context switching is
tested in Linux/glibc, not in the macOS harness.

## Actual results and failures

The macOS driver, new partial-admission/drain cases, ThreadSanitizer run and all
13 experiment-tool tests passed.

The first Linux attempt passed all three new standalone builds, 20 repetitions,
and both existing driver builds, then stopped with one of 13 experiment-tool
tests failing: the archive lacked Git metadata, so the runner exited during
source-diff collection. It did not reach an engine build. That attempt's logs,
source hashes, exit status **1**, and original two changed fixture/script files
are retained under [attempt1](attempt1/). The staging was corrected by supplying
the source-base Git metadata; a preflight was added before tests. The queued-low
fixture was also made to execute the supplied low computation on the retry.
The complete validation was rerun rather than treating that attempt as a pass.

During final collection, the copied `--no-checkout` Git metadata still had an
empty index, so the first exported compatibility diff incorrectly showed file
deletions. Rebuilt that disposable index with `git reset --mixed HEAD`, verified
all eight source hashes unchanged, and reran all 13 metadata-dependent experiment
tests successfully. The corrected `build-compatibility.diff` is byte-identical
to the MAC-01 patch; the discarded export is retained as
`unindexed-source.diff.gz`. [Repair/source verification](git-index-repair.log) ·
[Passing experiment recheck](experiments-indexed.log). No runtime source or
binary changed during this repair, so the engine/scheduler results were retained.

The final Linux run exited **0**:

| Check actually run | Result |
| --- | --- |
| New standalone suite, debug / optimized / UBSan | All 11 cases passed in each build: nine transition cases, partial admission and drain expiry. |
| Optimized repetition | 20 additional suite executions passed all 11 cases each, within the per-execution timeout. |
| Existing driver, contexts disabled / enabled | Both passed, including retries, callback failure, cancellation, routing and Cooperative regression checks. |
| Experiment tools | All 13 tests passed after the Git-metadata staging correction. |
| Engine build | `dbtest`, `tpcc_mixed_test`, `silo_context_test` built from the new source directory with the archived compatibility patch. |
| New real Silo Q2 matrix | All nine cases passed. The active Q2 checksum was `12860241382490130518` in every recorded case, matching its independent unpaused oracle. Across these cases: 36 committed stock writes, 45 scripted high attempts, 15 handoffs and 15 resumptions. |
| Existing engine checks | Q2 region/checkpoint oracles, New-Order/Payment rollback/retry, live snapshot isolation, nested protection, cleanup and reclamation all passed. |
| Independent raw-log replay | [Verifier](verify-records.py) reconciled **262 CASE blocks** from all 23 final standalone suite logs and the nine engine cases; checked unique IDs, every terminal outcome, admission/attempt counts, timestamps, active checksums and summary counts. [Replay result](reconciliation.log) · [Per-case counts and log hashes](reconciliation.json) |

Representative raw results: [optimized standalone](batched-optimized.log),
[real Silo Q2 and short-transaction checks](tpcc-mixed.log),
[context checks](silo-context.log), [full command/exit log](validation.log),
[guest environment](environment.log), [tested source hashes](source.sha256),
[binary/log hashes](artifacts.sha256), [build output](build.log.gz).
Host replay also verified all eight tested source hashes against the current
workspace. No experiment batch or performance measurement was collected for
MAC-07. The isolated test container was stopped after evidence collection;
earlier evidence containers remained stopped.

## Remaining acceptance

MAC-07 remains **partial** until its integrated cases are repeated with MAC-09's
actual signal backend. These results establish the recorded queue/checkpoint
paths and real Silo Q2 continuation, not asynchronous switching, signal-state
preservation or Intel UINTR behavior. Native hardware coalescing, delivery/return
races and performance remain I15/MAC-06. The notification model is a reusable
MAC-10 prerequisite; the broader MAC-10 acceptance is not declared complete here.
