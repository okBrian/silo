# MAC-08 local starvation prevention

November 5, 2026 · `erin/local-replication-evidence` · source base
`2bb11b94ed493f4c27c87abe8185573d3f077093` plus archived source changes.
**Local checkpoint scheduling in emulated x86-64 Linux.** MAC-09 signal repetition
and native UINTR/cycle validation remain pending.

## Implementation and scope

The shared [driver](../../../benchmarks/request_driver.cc) now uses
[StarvationBudget](../../../benchmarks/starvation_budget.h) for producer admission,
checkpoint handoffs and return from high service. The worker publishes a coherent
copy under a dedicated mutex when a low request starts/ends and after each high
request. Admission reads that same synchronized copy. The low budget resets once
per request, retains its start and charges across low retries, and charges an
entire high request including preparation, conflict retries and cleanup.
High-to-low enforcement is at the end of the high request. It can overshoot by
that entire request; there is no instantaneous CPU cap.

`--queued-starvation-threshold P` and the runner's `starvation_threshold` config
field accept integers from 0 to 100. The default is 100. Manifests identify the
clock as `CLOCK_MONOTONIC nanoseconds`; deterministic fixtures inject a shared
logical nanosecond clock. Neither measures serialized native cycle costs.
The TSV adds `low_service_ns`, elapsed execution on the low context excluding
high-service pauses. It includes fixture waits and guest scheduling delays, so
it is not a CPU-time measurement. Stderr separates admission/checkpoint blocks,
forced low returns, low dispatches and charged high/low service.

Wait retains its high-first boundary dispatch. Cooperative and the modeled
notification checkpoint backend use the shared budget/return loop. The budget
protects an active low request. It does not promise bounded wait for a queued Q2
under infinite high arrivals. Fixed-routing global FIFO admission still stops
at its first blocked head, including a budget block; this can delay other
workers. See the [contract](../../docs/experiment-contract.md).

## Controlled schedules and oracles

[Shared cases](../../tests/starvation_cases.h) run through a standalone arithmetic
computation and the real Silo Q2 fixture, retaining Silo's owning thread/TLS.
The latter executes committed stock writes while Q2's snapshot is suspended,
and compares every committed Q2 checksum to an independently run unpaused Q2.
The high conflict and low retry in the shared schedule are driver-injected;
the Q2 computations themselves succeed on both low attempts. The existing
engine checks separately exercise a real failed Q2 commit and actual
New-Order/Payment rollback, retry and linked-row/balance checks.

All controlled schedules use seed 1 and logical nanoseconds:

- **50% equality, overrun, recovery and retry:** start Q2 at 100, publish four
  shorts at 110. The first high request conflicts once and consumes 5 + 5 ns,
  finishing at 120. Its charged fraction is exactly 10/20 = 50%, and the second
  high is actually dispatched. It finishes at 130, with 20/30 > 50%, forcing low
  return. The producer catches up the second four-short batch due at 120 and
  observes a blocked admission snapshot. At 139, the checkpoint still refuses
  to switch. At 140, 20/40 = 50% permits admission and switching again. Further
  low slices recover the budget between highs. Six highs finish during the
  active Q2 across five forced returns. Its controlled retry retains low start
  100 and all 60 charged high ns. A subsequent low request starts with zero charge.
- **Drain expiry:** the same first Q2 commits after its retry at the exact 1130
  drain deadline. Two still-global highs and one queued Q2 are cancelled with
  terminal records. The fixture waits for producer shutdown before returning,
  making that cancellation expectation deterministic.
- **0% / default 100%:** one four-short batch arrives during Q2. At 0%, the
  zero initial charge permits the first high, then the active Q2 runs to commit
  with remaining high work queued. At 100%, all four shorts execute during Q2,
  with no budget-forced return. Both cases complete the next Q2 with a reset
  budget. This demonstrates that 100% is normally nonrestrictive.

## Commands and environment

The Linux run uses a new idle-entrypoint `silo-mac08-validation` container from
`silo-mac07-tools:local` in the existing Colima/QEMU x86-64 guest, with no host
mounts. The earlier evidence containers stay stopped. Source was a fresh
`git archive` of the base plus current tracked files/new MAC-08 files; a detached
local Git copy supplies runner metadata. The pinned Masstree cache is cloned at
`1150824e6a10a0384ba4bfed6b916ce62f96315f`. Copied files are reassigned to the
container owner before Git preflight. No prior object files are staged.

```bash
cd /mac08-source
bash replication/tests/validate_starvation.sh /mac08-evidence
```

[Validation script](../../tests/validate_starvation.sh) records compiler commands,
source hashes and the actual environment; standalone debug, optimized and UBSan
builds; 20 optimized repetitions; Wait/Cooperative driver regressions; experiment
unit tests; and all three engine target builds. The archived MAC-01 formatter and
`-O1` compatibility patch applies only inside the disposable guest copy. Each
standalone execution has a 60-second timeout, each engine test 120 seconds.
The final stage runs the two saved pressure configs, analysis and report gates.

On macOS, ran the standalone Wait driver with Apple Clang, C++11, `-O2`,
`-Wall -Wextra -Werror -pedantic -pthread`. Ran the dedicated two-worker
[concurrent snapshot test](../../tests/budget_publication_test.cc) with the same
warnings and `-O1 -g -fsanitize=thread`. Also ran:

```bash
python3 -B -m unittest discover -s replication/tests -p test_i05_experiments.py -v
```

## Actual results

The saved runtime checks and both pressure analyses/report gates passed. The
initial full script stopped with **exit 2** at its analysis invocation after the
25% runtime batch. The retained [recovery command log](pressure-recovery.log)
finished analysis, ran the 100% batch and both report gates with **exit 0**.
The corrected validation script is syntax-checked; its corrected collection
commands are the commands exercised in recovery. The entire corrected script
was not rerun from the beginning.

| Check actually run | Result |
| --- | --- |
| Standalone debug / optimized / UBSan | All four controlled cases passed per build: 0%, 100%, 50% recovery/retry, and 50% drain expiry. |
| Optimized repetition | 20 additional executions passed all four cases each. |
| Real Silo Q2 budget cases | All four passed, with seven Q2 commits, 22 committed intervening/boundary stock writes, and active checksum `12860241382490130518` matching each unpaused oracle. The expiry case retains two cancelled global highs and one cancelled queued Q2. |
| Engine regressions/build | Fresh-built `dbtest`, `tpcc_mixed_test` and `silo_context_test`; both engine suites passed, including the nine MAC-07 notification cases. |
| Existing driver regressions | Both Wait and context-enabled builds passed. |
| Experiment tools | All 14 tests passed in Linux and on the Mac, including threshold/clock manifest and invalid-range checks. |
| Mac concurrent snapshot test | Two workers and an independent reader passed ThreadSanitizer; 112,238 coherent snapshots in this run. |
| Independent controlled replay | [Verifier](verify-records.py) reconciled **96 starvation CASE blocks / 768 terminal records** from the 23 final standalone logs plus four engine cases. |
| Archive regeneration | Both saved pressure batches regenerated byte-identical summary/per-type/health CSVs and reports on the Mac. [Replay](replay.log) |
| Source identity | Eleven tested runtime/config/fixture files match current workspace hashes; the original and corrected validation scripts have separate verified hashes. [Verification](source-verification.log) |

The pressure runs use the real benchmark's New-Order/Payment adapters and full
Q2 dataset, Cooperative at 64 reads, one or two workers, seed 1, four shorts per
worker every millisecond, no warmup, a three-second measurement window and a
ten-second drain. Each threshold/worker configuration ran **once** on the same
binary. These are functional overload checks; they do not establish a comparative
performance result. CPU affinity is the inherited guest mask, not individual
worker/sender pinning.

| Threshold | Workers | Q2 commits (in window) | Short arrivals | Short commits | Queue-full rejections | Forced low returns | Admission blocks | Committed low-context elapsed ns |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 25% | 1 | 1,500 (1,499) | 11,996 | 8,744 | 3,252 | 1,961 | 34 | 2,273,840,368 |
| 25% | 2 | 2,347 (2,345) | 23,992 | 12,527 | 11,465 | 1,895 | 35 | 4,901,829,786 |
| 100% | 1 | 1,590 (1,588) | 11,996 | 8,754 | 3,242 | 0 | 0 | 2,273,360,360 |
| 100% | 2 | 2,172 (2,168) | 23,992 | 12,414 | 11,578 | 0 | 0 | 4,813,110,185 |

Totals: **7,609 Q2 commits** (7,600 before cutoff), **42,439 short commits**,
and **29,537 rejected shorts**, covering all **71,976 scheduled short arrivals**.
The two-worker 100% run recorded one real short conflict/retry; the other pressure
runs recorded zero aborts. No pressure request was cancelled or unaccounted for.
All committed Q2 records retain valid checksums and positive `low_service_ns`;
exact snapshot checksum correctness is additionally checked in the deterministic
engine cases above. Restrictive 25% triggered 3,856 forced returns and 69
admission blocks. Default 100% triggered neither, as expected.

[Independent replay and counts](reconciliation.json) ·
[25% per-type report](report-25/report.md) · [100% per-type report](report-100/report.md) ·
[Engine records](tpcc-mixed.log) · [Standalone records](starvation-optimized.log) ·
[Guest environment](environment.log) · [Container identity](container.log) ·
[Tested source hashes](source.sha256) · [Binary hashes](artifacts.sha256) ·
[Build output](build.log.gz) · [Source changes](source-changes.tar.gz).
The guest is Ubuntu 22.04.5, GCC 11.4/glibc 2.35 with two emulated vCPUs.
The isolated container was stopped after collection; prior containers stayed stopped.

## Failures retained

The first staging command encountered Git ownership guards in the disposable
source/cache. Reassigning those copies to the container owner fixed staging.
The first controlled fixture expected four forced returns; its schedule actually
requires five (the initial two-high group, then four single-high groups).
The corrected-count run exposed a race in the drain fixture: the queued low
could start before the producer expired its deadline. The fixture now waits for
producer shutdown while the active low is still running. The first two failed logs are retained in `attempt1/` and `attempt2/`, with
binary hashes in `removed-binaries.sha256`; neither is counted as a passing
final execution. Binaries remain in the stopped disposable guest. These failures did not require changes to the budget
predicate or scheduling behavior.

The first full workflow's analysis call placed the global `--output` option
after the subcommand and pointed at the parent of the generated batch. The
script now selects the single generated `batch.json` directory and places
`--output` before `batch`. The original [script](validation-attempt1.sh),
[full log](validation.log), [exit 2](validation-attempt1-exit-status), and tested
script hash are retained. [Recovery commands](pressure-recovery.sh) completed
both analyses and report gates, ran the remaining 100% cases and recorded
[exit 0](pressure-exit-status). No engine/runtime source or binary changed.
The corrected script's hash is saved separately. The Mac Python bytecode syntax
check initially hit its external cache-directory permission; an in-memory
compile check succeeded without writing that cache.

## Remaining acceptance

MAC-08 remains **partial** until its cases repeat through MAC-09's actual
Linux-signal backend. This task implements no asynchronous handler and supplies
no native UINTR evidence. The 100% setting is exercised, not claimed to restrict
high-service share. Timings and ratios describe local elapsed time in the
recorded guest, not the paper's hardware performance. Independent native cycle
reads, native entry/return races and final performance remain MAC-06/I16–I18.
