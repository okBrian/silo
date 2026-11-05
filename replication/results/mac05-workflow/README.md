# MAC-05 validation and collection workflow

November 5, 2026 · **Emulated Linux functional validation** ·
Branch `erin/local-replication-evidence`, source base
`da4dcec0e86b9a6255f979157d268952ffde4d81`.

## Deliverables and source

The [clean-build and smoke script](../../experiments/validate-local.sh) builds in a
new checkout, verifies the pinned Masstree revision, runs the two engine tests,
driver and experiment tools, executes the existing paired smoke, saves raw records
and manifests, and replays the archived batch. The [report command](../../experiments/report.py)
adds policy/type labels, all four percentiles and sample counts, throughput and
abort/outcome counts, and explicit incomplete-request accounting. Failed and partial
runs stay visible and fail the optional complete-batch gate.

The [host workflow](../../experiments/HOST_WORKFLOW.md) gives Intel build/smoke and
replay commands, acceptance rules and the native collection handoff. The
[Figure 10 target](../../experiments/plans/figure10.json) is planning-only: it sets
16 workers/warehouses, a distinct sender core on the same socket, matched input and
queue settings, 5/30-second phases, five varied three-policy orders and the report
requirements. Source/binary and CPU IDs remain unset until native integration and
host qualification. Current execution supports Wait and Cooperative only.

The local run reused container `39882f945f31` (`silo-mac01-mac02`) in the existing
Colima/QEMU x86-64 guest. The container image is
`sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11`
(Ubuntu 22.04); it has no host mounts. It retains the MAC-01 packages (GCC 11.4,
glibc 2.35, Python 3.10). The input archive was a detached clone of source base
`da4dcec`, with the new workflow tools and experiment tests copied in. The script
made another clean clone for the build, initialized Masstree at
`1150824e6a10a0384ba4bfed6b916ce62f96315f` from the cached checkout, and applied the
same `-O1`/GCC pair-formatter compatibility patch as MAC-01 solely in that copy.
No prior build outputs were reused. Exact tools are retained with their hashes.

**Execution deviation:** starting this previously stopped container also restarted
its old `/validate.sh` entrypoint, which began a separate MAC-01/MAC-02 rerun. This
overlapped part of the MAC-05 compilation. The old runner and its active benchmark
were paused (PIDs 3737 and 3817) before the MAC-05 engine tests and smoke; the
MAC-05 process tree remained running. None of that unintended batch is used here.
The repository's original MAC-01/MAC-02 archive was preserved. Future container
runs should use a fresh container with an idle entrypoint rather than restarting
the archived validation command. These build and smoke results remain functional
checks, with no performance comparison inferred from compilation or emulation.
After collecting MAC-05, the container was returned to its original stopped state.
[Container identity](container-environment.log) · [Paused-process snapshot](container-processes.log)

## Commands actually executed

The input was staged with `git clone --no-local --no-checkout`, detached at
`da4dcec`, and transferred with `docker cp`. In the running container:

```bash
mkdir /mac05-input
tar --no-same-owner -xf /mac05-source.tar -C /mac05-input
git -C /mac05-input/source -c protocol.file.allow=always \
  -c submodule.masstree.url=/work.35jZzy/source/masstree \
  submodule update --init --checkout masstree
MAC05_ENVIRONMENT='emulated x86-64 Linux functional validation on Colima/QEMU; Ubuntu 22.04' \
  bash /mac05-input/source/replication/experiments/validate-local.sh \
  /mac05-input/source /mac05-evidence
```

The standalone [independent metric check](verify-replay.py) reads raw measurement
TSVs and verifies every per-type latency distribution, percentile, sample count,
low-sample flag, throughput, attempt/abort count and exit reconciliation against the
new report. It was also applied to the unchanged MAC-02 archive.

## MAC-02 saved-batch replay

On macOS, extracted `mac01-mac02/batch.tar.gz` to a new temporary directory, ran
`analyze.py`, `report.py --require-complete`, `check_cooperative_batch.py` and
`verify-replay.py`, then used `cmp` against the original `queued-summary.csv`.
All passed. The eight runs regenerated 24 per-type measurement rows and all original
metrics: **8,125 commits, 432 shorts and 36 Cooperative handoffs**. Short per-type
samples were only 16–37 per run; Q2 samples were 715–1,247. These remain smoke
estimates. [Independent metric results](mac02-metric-replay.log) ·
[Trace reconciliation](mac02-trace-replay.log) · [Per-type CSV](mac02-per-type.csv)

## Fresh clean-build results

- `dbtest`, `tpcc_mixed_test` and `silo_context_test` built from the clean isolated
  checkout with `WORKER_CONTEXTS=1`. Both engine tests passed, including Q2 checksums
  across nine write handoffs, short-transaction rollback/retry, snapshot/context
  isolation, nested protection, conflict cleanup and deferred reclamation.
- The standalone request driver passed its overload, shutdown, retry, fatal-error,
  notification/batching, starvation-budget, Wait and Cooperative tests. Negative
  fixtures intentionally produce failures/cancellations; these are expected test
  scenarios, not failures in the eight-run benchmark batch.
- All **13 experiment-tool tests** passed in the guest (and earlier on macOS),
  including the two new report checks for separate outcome/sample accounting and
  preservation of failed/incomplete/unknown results.
- All eight fresh benchmark invocations exited zero. All 16 warm-up/measurement
  phase summaries reconciled. Every one of the **7,910 measurement requests**
  committed, including **432 shorts** and **7,478 Q2s**, with zero aborts,
  rejections, cancellations, failures or missing terminal records. All four policy
  pairs matched short inputs/deadlines, shared Q2 input prefixes and CPU masks.
  Wait had zero same-worker overlap. Cooperative made **40 measurement handoffs**
  and served **160 shorts** while Q2 was paused.
- The script extracted its own saved archive and regenerated byte-identical
  `queued-summary.csv`, `per-type.csv`, `run-health.csv` and `report.md`. The
  independent raw-record verifier passed all 24 per-type measurement rows.
  A second extraction and full replay on macOS using the saved tool snapshots also
  produced identical CSVs/report and passed the metric and trace checks.
  Per-run New-Order samples were 20–37, Payment 16–36 and Q2 670–1,255. The report
  marks small-tail estimates; all these one-second results are smoke evidence.

| Policy | Measurement commits | Short commits | Q2 commits | Window commits | Drain commits | Mid-Q2 handoffs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wait | 3,990 | 216 | 3,774 | 3,980 | 10 | 0 |
| Cooperative | 3,920 | 216 | 3,704 | 3,911 | 9 | 40 |

Measurement commits include drain completions. This table is functional accounting,
not a performance comparison. [Environment](environment.log) · [Build log](build.log.gz) ·
[Exact validation commands and results](validation.log) · [Exit status 0](exit-status) ·
[Compatibility patch](build-compatibility.diff) · [Tested tool snapshots](tools/) ·
[Tool hashes](tools.sha256) · [Raw batch archive](batch.tar.gz) ·
[Trace reconciliation](reconciliation.log) · [Independent metric check](fresh-metric-replay.log)

[macOS archive replay](host-replay.log) and [final host checks](host-checks.log)
also passed: saved tool hashes, Bash syntax, native-plan JSON/order validation,
local documentation links and `git diff --check`.

The [collection report](analysis/report.md), [per-type CSV](analysis/per-type.csv),
[run health](analysis/run-health.csv), [analysis JSON](analysis/analysis.json) and
[original analyzer CSV](analysis/queued-summary.csv) retain every repetition.
`outstanding_at_cutoff` includes pending global, queued, running/suspended and due
ungenerated requests. `unaccounted_at_exit` is zero only after phase reconciliation;
noncommitted terminal outcomes stay separately counted. Replay commands are in the
[host workflow](../../experiments/HOST_WORKFLOW.md#replay-without-a-linux-guest-or-dbtest).

## Limits and next task

This task does not complete I17 or I18. The upstream full suite, full integrated
stress/consistency matrix, actual UINTR delivery, asynchronous context preservation,
Preemptive Silo scheduling, hardware batching races and starvation/Q2 progress under
load remain open. No native Intel performance run, three-policy comparison or final
Figure 10 plot was executed. The current plotter only provides overall p50/p99
diagnostics. The next hardware work is MAC-06: qualify the host, pass I12, then
finish native I13–I17 before collecting the planned I18 results. Check remaining
course-rubric coverage separately.
