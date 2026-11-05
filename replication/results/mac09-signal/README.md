# MAC-09 Linux-signal backend

November 5, 2026 · `erin/local-replication-evidence` · source base
`c7989984e6e6795d88046157cef7c6d40e33d133` plus archived source changes.
**Linux-signal backend in emulated Linux.** Native Intel UINTR delivery,
entry/return preservation and performance remain MAC-06/I12–I18.

## Implementation and limits

The opt-in CLI/config policy is `linux-signal`, distinct from Wait, Cooperative,
the C++ notification model and native Preemptive. Per-worker SIGUSR2 delivery
captures a real kernel signal frame only within bounded, audited assembly
computation windows. The handler returns to a separate landing stack; ordinary
code switches to high service, resumes, then restores the original complete
kernel frame with the x86-64 signal restorer. No glibc context API, allocator,
logger or mutex executes in the handler. Deferred signals can be re-armed;
ordinary queue fallback remains separately distinguishable from actual captures.
The [full contract](../../docs/linux-signal-backend.md) defines masks, guarded
stacks, supported state, TLS restrictions, transition/teardown rules and scope.

In Silo, the windows occur at audited completed Q2 reads with high work queued
and budget permission. Captures are asynchronous within those windows, not
general interruption anywhere inside Silo/libc. The standalone independent
sender additionally requires capture inside an arithmetic loop, distinguished
from delivery at the window's signal-unblock syscall return. Exact arithmetic
and legacy x87/MXCSR/XMM0–15 state are checked. Separate saved-R12 and saved-XMM0
corruption controls must be detected. AVX/AMX/CET and native UINTR state are not
claimed validated.

The MAC-07 fixture preserves its controlled model schedules and adds real
signal publication/delivery. Model events are not real handler counts; the
signal policy sends once per actual publication in addition to modeled
deliveries. Partial-admission and drain-expiry intentionally block delivery
through low completion, so their zero captures are boundary/teardown checks.
The nine transition cases require actual captures and exact low continuation.
MAC-08 repeats its budget fixtures and real New-Order/Payment/Q2 pressure.
High-request budget enforcement and overshoot remain transaction-boundary rules.

## Environment and commands

The [Mac host](host-environment.log) is the Apple M5 Pro development machine.
The existing Colima/QEMU x86-64 Linux guest was reused with a fresh, separate
`silo-mac09-validation` container from `silo-mac07-tools:local`, idle `sleep`
entrypoint, and no host mounts. Earlier evidence containers stayed stopped.
The source is a new archive of the base with changes overlaid, detached Git
metadata for the experiment runner, and a fresh no-hardlink clone of pinned
Masstree `1150824e6a10a0384ba4bfed6b916ce62f96315f`. No previous object files
were staged. Ubuntu 22.04.5, GCC 11.4 and glibc 2.35 run on two emulated vCPUs.
The archived MAC-01 GCC formatter/`-O1` patch applies only to the disposable
guest before building the three engine targets.

```bash
cd /mac09-source
bash replication/tests/validate_signal_backend.sh /mac09-evidence/final
python3 -B replication/tests/verify_signal_records.py /mac09-evidence/final
```

The [validation script](../../tests/validate_signal_backend.sh) records exact
commands, source/environment/binary hashes, three builds with assertions retained,
20 optimized repetitions per suite, the old checkpoint schedules, both driver
builds, experiment tools, fresh engine builds and four pressure runs. Individual
tests have 60/120-second timeouts with a five-second forced-kill fallback.
The [independent verifier](../../tests/verify_signal_records.py) reconciles raw
terminal rows, IDs, timestamps, outcomes, attempts, checksums, signal counters,
budget expectations and all scheduled pressure arrivals.

The Mac separately compiled and ran the existing Wait driver and partial/drain
harness with Apple Clang, C++11, `-O2 -Wall -Wextra -Werror -pedantic -pthread`.
All 15 Python experiment-tool tests passed there. These host checks make no
Linux signal/context claims. [Driver](host-driver.log) · [Batching](host-batched.log) ·
[Tools](host-experiments.log).

## Actual results

Both the initial full validation and the final focused fixture follow-up exited
**0**. Final independent replay on Linux and the Mac reconciled **358 controlled
CASE blocks / 2,570 terminal records**, **69 state checks**, and all four pressure
runs. The two collections are distinguished below; neither failed attempt is
counted as a final pass.

| Check actually run | Result |
| --- | --- |
| Standalone asynchronous state checks | Debug, optimized and UBSan plus 20 optimized repetitions passed 69 cases: 23 normal, 23 deliberate R12 corruptions and 23 deliberate XMM0 corruptions. Across them: 1,518 captures/returns; the normal runs include 1,382 arithmetic-interior captures. Nested deferral, repeated/coalesced signals, blocked high-service masks, exception unwinding and cleanup passed. |
| Final MAC-07 standalone fixtures | All 11 cases passed in all three builds plus 20 optimized repetitions, including nine transition cases, two-worker partial admission and bounded drain expiry. |
| Final MAC-07 real Q2 matrix | Nine cases preserved the same-attempt snapshot checksum `12860241382490130518`, with 36 committed intervening writes and 15 actual signal captures/handoffs/resumptions. |
| Final MAC-08 fixtures | All four cases passed in all three builds plus 20 optimized repetitions; four real Q2 cases preserved the oracle through seven Q2 commits and 22 committed high writes. Exact threshold, overrun/recovery, retry/reset, 0%/100% and drain cancellation passed. |
| Actual paused-write abort cleanup | The final signal engine fixture passed **53 real failed-write/abort probes while Q2 was suspended**. Each checks unchanged stored values and cleared protection before committing the intended high write. These extra probes are separate from the scripted driver conflict/attempt counts. |
| Builds and regressions | The initial full script fresh-built all three engine targets, passed both driver builds, all 15 tools tests, checkpoint fixtures and `silo_context_test`. The final fixture-only rebuild passed the strengthened engine test in both signal and checkpoint modes and repeated both standalone checkpoint suites. |
| Handler inspection | Optimized and engine disassemblies use direct static TLS and only the local saturating counter helper and `sigfillset`; no allocator, lock, logger or glibc context switch occurs in the handler. |
| Source identity and replay | [Current source verification](source-verification.log) matches tested runtime and final fixture hashes. [Runtime/binary continuity](final/runtime-verification.log) confirms the full validation's runtime and pressure binary are unchanged. Both pressure archives regenerated byte-identical CSVs/reports on the Mac. [Report replay](final/host-report-replay.log) |

Pressure uses one repetition per threshold/worker configuration, seed 1, four
shorts per worker every millisecond, no warmup, three seconds of measurement and
a ten-second drain. It uses real benchmark Q2/New-Order/Payment adapters on the
same binary, with the inherited guest CPU mask; individual workers/sender are
not pinned. Local monotonic nanoseconds and elapsed low-context time are not
native cycles or CPU-time measurements. These are functional checks, not matched
three-policy performance evidence.

| Threshold | Workers | Q2 commits | Short arrivals | Short commits | Queue-full rejections | Actual captures | Forced low returns | Admission blocks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 25% | 1 | 1,181 | 11,996 | 7,693 | 4,303 | 1,649 | 3,870 | 283 |
| 25% | 2 | 1,826 | 23,992 | 10,813 | 13,179 | 2,661 | 3,445 | 235 |
| 100% | 1 | 1,415 | 11,996 | 8,960 | 3,036 | 1,423 | 0 | 0 |
| 100% | 2 | 2,155 | 23,992 | 12,283 | 11,709 | 2,732 | 0 | 0 |

Totals: **6,577 Q2 commits**, **39,749 short commits**, **32,227 rejected shorts**,
covering every one of **71,976 scheduled short arrivals**. No pressure request
was cancelled or missing. Every committed Q2 has a checksum and positive
low-context service. These pressure runs recorded **zero real short conflicts**;
retry/rollback coverage comes from controlled cases and the engine checks.
Restrictive 25% triggered **7,315 forced returns / 518 admission blocks**;
100% triggered neither. The pressure runs recorded **8,465 actual signal captures**
and **14,065 total mid-low handoffs**, hence **5,600 voluntary fallbacks**. Only
actual captures are counted as asynchronous switches. Whole-request overshoot
and fixed FIFO head blocking retain the MAC-08 interpretation.

[Independent replay](final/reconciliation.log) · [Raw replay counts](final/reconciliation.json) ·
[Final signal Q2/abort checks](final-fixtures/tpcc-signal.log) ·
[Final checkpoint regression](final-fixtures/tpcc-checkpoint.log) ·
[Final fixture commands](final-fixtures/validation.log) ·
[Initial full command log](final/validation.log) · [Guest environment](final/environment.log) ·
[25% report](final/report-25/report.md) · [100% report](final/report-100/report.md).
Final fixture [source hashes](final-fixtures/source.sha256) and
[binary hashes](final-fixtures/artifacts.sha256) supersede the initial fixture
versions; the initial runtime/state/pressure hashes remain applicable.

**MAC-09 is complete locally for its documented windows/state. MAC-07 and MAC-08
are now complete locally after signal-backend repetition.** Hardware-specific
UINTR delivery/coalescing/return races, native cycle costs and final performance
remain MAC-06/I12–I18. MAC-10's broader acceptance assessment and MAC-11's report
package remain independent work.

## Retained development failures

Initial source staging hit Git ownership checks, AppleDouble metadata appearing
as invalid pack indexes and a cached-dependency hardlink-copy failure. The
disposable source was reassigned to root, its `._*` metadata files removed, its
index populated, and Masstree cloned with `--no-hardlinks`; earlier containers
and the Mac checkout were unaffected.

The first arithmetic-interior assertion excluded the loop target instruction.
QEMU delivered at that translated loop boundary, so a passing continuation run
failed the classification assertion. A distinct `nop` at the unblock-return
label separates it from the arithmetic loop, and final normal runs require
positive interior captures. The first signal batching check consumed its sole
notification under protection and therefore used only voluntary queue fallback;
the missing deferred-signal re-arm path was implemented rather than counting
that fallback as asynchronous service. The early full validation then stopped
at a starvation-fixture compile error (missing local counter variable); the
corrected complete script was restarted. Failed logs/source hashes are retained
under `attempt1/` and `attempt2/`; their results are not final passing executions. Early ad hoc checks retain logs rather than every intermediate source variant.


The first follow-up real-abort probe passed in signal mode, but the subsequent
checkpoint regression exposed a controlled-clock fixture race: the low callback
could advance logical time to cutoff before the producer queued the next low
request. The reset assertion then saw only one low request. The final fixture
waits until the first producer pass (including low publication) finishes before
advancing time, in both recovery and 0%/100% cases. This changes fixture
synchronization, not the budget predicate or runtime. That follow-up also counted
boundary writes; final probes explicitly require a live, suspended Q2, excluding
boundary drains. The first follow-up's exit 134 and its source/logs are retained
under `attempt3/`.

The final focused follow-up uses:

```bash
bash replication/tests/validate_signal_fixtures.sh /mac09-evidence/final-fixtures
python3 -B replication/tests/verify_signal_records.py /mac09-evidence/final \
  --fixtures /mac09-evidence/final-fixtures
```

It rebuilds both controlled suites in all three variants, repeats each optimized
suite 20 times, repeats checkpoint regressions, then rebuilds and runs the final
engine fixture in signal and checkpoint modes. Runtime/backend/benchmark sources
and the pressure binary are unchanged from the initial full validation, so the
four pressure runs, 69 state checks, both driver builds and context regression
remain that collection's evidence. The entire full validation was not rerun
after these fixture-only improvements. Future fresh runs of the main validation
script use the strengthened fixtures directly.


The isolated validation container was stopped after final collection; its
executables remain there and are excluded from the repository evidence copy.
Their hashes are retained. The pre-existing containers remain stopped.
[Source base](source-base) · [Final source changes](source-changes.tar.gz) ·
[Tracked diff](source-tracked.diff) · [Verification-tool hashes](verification-tools.sha256) ·
[Container identity](container.log) · [Excluded executable hashes](excluded-binaries.sha256).
