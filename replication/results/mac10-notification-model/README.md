# MAC-10 local notification model

November 5, 2026 · `erin/local-replication-evidence` · source base
`6e3a0bae9353ee651ae96a2fdd150d257dfa4c9c` plus archived changes.
**Software notification model with checkpoint service in emulated Linux.**
Real signal execution is separately labeled as a regression. No native UINTR
capability/delivery or performance test was attempted for MAC-10.

## Implementation

Extended the existing pending-bit transport with attempted-delivery counters,
quiescent close, pending-bit cancellation and post-close drop accounting. Added
an ordered event schedule with a logical clock, deterministic equal-time
ordering and per-event state/counter traces. The new receiver fixture is shared
between synthetic arithmetic and real Silo Q2, and the starvation fixture now
uses scheduled send/deliver pairs for each actual admission publication.

The [model contract](../../docs/notification-model.md) defines publication
ordering, pending-bit semantics, protection, exact event schedules, counters
and receiver lifetime assumptions. The original MAC-07 transition/partial/
drain cases remain in the acceptance matrix. The new cases demonstrate both
early and withheld delivery, a real high-request retry, two nested protection
checks, same-attempt low resumption, stale hints after service and dropped
events after receiver destruction. Silo repeats them with committed stock
writes while the original read-only Q2 snapshot is paused.

## Environment and commands

Reused the existing Colima/QEMU x86-64 guest on the Apple M5 Pro Mac. The fresh
`silo-mac10-validation` container uses `silo-mac07-tools:local`, an idle `sleep`
entrypoint and no host mounts. Existing validation containers stayed stopped.
The Linux source copy contains no reused engine objects. Masstree is a fresh
`--no-hardlinks` local clone at pinned revision
`1150824e6a10a0384ba4bfed6b916ce62f96315f` from the prior dependency cache.
The MAC-01 GCC formatter/`-O1` compatibility patch applies only inside that
disposable source before the engine build; the Mac checkout is unpatched.
The recorded guest is Ubuntu 22.04.5, GCC 11.4/glibc 2.35, with two emulated
vCPUs and inherited process affinity. [Guest inventory](final/environment.log) ·
[Container](container.log).

```bash
cd /mac10-source
bash replication/tests/validate_notification_model.sh /mac10-evidence/final
python3 -B replication/tests/verify_notification_model.py /mac10-evidence/final
```

The [validation script](../../tests/validate_notification_model.sh) records all
compiler commands and individual test timeouts, source/environment/binary
hashes, the three controlled suites in debug/optimized/UBSan builds and 20
optimized repetitions per suite, both driver builds, experiment-tool tests,
a fresh `tpcc_mixed_test` engine build and checkpoint/signal execution modes.
The [independent verifier](../../tests/verify_notification_model.py) replays
every saved `EVENT` state/counter transition and reconciles raw terminal IDs,
outcomes, attempts, timestamps, checksums and phase totals.

The Mac separately ran the transport-only test with Apple Clang, C++11, `-O2
-Wall -Wextra -Werror -pedantic -pthread`, then with
`-fsanitize=address,undefined`. Each passed three transport checks and recorded
18 events; these builds do not run the glibc worker contexts or signal backend.
The existing Wait driver and all 15 experiment-tool tests passed on the Mac.
[Host environment](host-environment.log) · [Transport](host-model.log) ·
[Sanitizers](host-model-sanitized.log) · [Driver](host-driver.log) ·
[Tools](host-experiments.log).

## Actual results

The complete final script exited **0**, including independent replay. The Mac
replayed the copied evidence with the same totals: **419 controlled case blocks,
2,848 terminal records, 822 event records and 94 traced schedules**. Those
totals include the separately labeled signal engine regression. The 69
standalone suite executions alone account for 391 case blocks / 2,668 terminal
records; their 805 event records all describe model events with zero handlers.

| Check actually run | Result |
| --- | --- |
| Transport and receiver suite | Debug, optimized, UBSan and 20 optimized repetitions passed three transport assertion groups and two receiver cases per execution. Covered routing, pending bits, delayed/no-op/duplicate/coalesced events, equal-time ordering, callback re-arm, protected deferral, stale hints, cancellation and post-destruction drop behavior. |
| MAC-07 standalone regression | All 11 cases passed in all three builds plus 20 optimized repetitions: nine transition schedules, two-worker partial admission and bounded drain cancellation. |
| MAC-08 standalone regression | All four cases passed in all three builds plus 20 optimized repetitions: exact threshold, overrun/recovery, retry/reset, 0%/100% and drain expiry. Scheduled send/deliver counts match actual publication passes. |
| Fresh Silo checkpoint fixture | All 15 controlled blocks passed, including the two new receiver schedules. Each new case serviced two committed stock writes while the same Q2 attempt was paused, then resumed with checksum `12860241382490130518`. Across the two new cases: four committed writes, two voluntary handoffs/resumptions, four protected deferrals and zero real handlers/captures. Stale hints caused no extra switch, and post-destruction events invoked no receiver. |
| Separate Silo signal regression | All 13 existing controlled blocks passed and the fixture completed 53 real failed-write rollback/cleanup probes while Q2 was paused. These are real Linux-signal handler/capture records, separate from the model's event traces. No new signal state or pressure suite was run. |
| Driver and experiment tools | Both Linux driver builds (Wait and contexts enabled) and all 15 Python tool tests passed. The Mac Wait driver and all 15 tools tests also passed. |
| Hardware kit continuity | All six kit files match their initial hashes in Linux and on the Mac. No kit source or native delivery status changed. |

[Full commands](final/validation.log) · [Exit status](final/exit-status) ·
[Guest replay](final/reconciliation.log) · [Mac replay](host-reconciliation.log) ·
[Replay totals and per-log counts](final/reconciliation.json) ·
[Q2 checkpoint records](final/tpcc-checkpoint.log) ·
[Separate signal records](final/tpcc-signal-regression.log) ·
[Build output](final/build.log) · [Tools](final/experiments.log) ·
[Kit verification](final/hardware-kit-verification.log).

**MAC-10 is complete locally under the documented software-model assumptions.**
MAC-11 can use this evidence. MAC-06/I12 native qualification and delivery remain
pending. This validation ran the entire final script after the fixture correction.

The [source base](source-base), [source overlay](source-changes.tar.gz),
[tracked diff](source-tracked.diff), [host source hashes](source.sha256),
[tested source hashes](final/source.sha256) and [source verification](source-verification.log)
identify exactly what ran. [Binary hashes](final/artifacts.sha256) are retained;
executables are excluded from the evidence copy and remain in the stopped
isolated container. [Excluded executable inventory](excluded-binaries.sha256).

## Failures and limits

The first setup attempt stopped before any acceptance tests: the transferred
detached dependency repository omitted its empty `.git/refs` directory, so Git
did not recognize the cache as a repository. Restored the empty directory,
cloned pinned Masstree and restarted. This affected only the disposable copy.

The next run passed the model and batching debug suites, then aborted in the
new starvation schedule. It incorrectly assumed that the eight generated
highs imply exactly two admission callbacks. A refill can span several passes
as capacity and budget recover. Corrected the fixture to apply an ordered
send/delivery pair to every actual publication and verify that send/delivery
counts equal the actual pass count. The complete validation was restarted;
failed-attempt logs and source hashes are retained separately and excluded
from final pass totals.
[Setup failure](setup-attempt1/validation.log) ·
[Fixture failure](fixture-attempt2/starvation-debug.log) ·
[Failed fixture source](fixture-attempt2/starvation_cases.h) ·
[Failed source hashes](fixture-attempt2/source.sha256).

Close is quiescent, not a racing unregister implementation. The transport and
schedule model the project's queue/publication contract, not all CPU/kernel
notification behavior. The model paths use voluntary checkpoint switching and
must report zero real handlers/captures. The signal regression uses MAC-09's
restricted audited windows and has separate handler/capture counters. No new
pressure benchmark, matched policy comparison, native latency measurement or
upstream full Silo suite is claimed here. The unchanged hardware kit remains
ready for later capability and receiver delivery checks; **I12 is prepared,
not hardware-validated**. Native UINTR semantics, entry/return preservation and
costs remain MAC-06.
