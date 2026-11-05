# Midpoint evidence freeze and claim index

Freeze: **November 5, 2026**, `erin/local-replication-evidence`, clean starting
revision **7319ba60394b6d68a4d8f6382177c5e5758c6b28**. MAC-11 adds report,
analysis and packaging files only. No engine, workload or scheduler source changed.
Masstree gitlink: `1150824e6a10a0384ba4bfed6b916ce62f96315f`.

The package contains that revision's full source/evidence snapshot plus the
MAC-11 report/tool/documentation overlay. `inventory.json` records every frozen
MAC-01–MAC-10 evidence file's SHA-256 and size. Old runs retain their **own**
base/overlay and executable hashes; a freeze is not a claim that they were
rerun on the latest revision. Executables remain excluded. Source archives and
failed-attempt logs are retained. There are no separately recorded teammate-Mac
executions. PACE history remains historical context, outside the report tables.

All E01–E10 paths below are relative to this directory. Their READMEs include
exact original build/run commands, environment details and failure records.

| ID | Saved evidence / source identity | Report claims and interpretation |
| --- | --- | --- |
| E01/E02 | [MAC-01/MAC-02](../results/mac01-mac02/README.md), `environment.log`, `validation.log`, `exit-status`, ABI logs and raw batch; base `8395dab` | Three builds and local baseline; ABI million-round-trip checks. Default 10,000-read run reached zero checkpoints with 843–2,363 eligible reads per Q2. Historical run, not rerun by MAC-11. |
| E03 | [UINTR kit](../results/mac03-uintr-kit/README.md), `build.log`, `checks.log`, `host-probe.log`, `host-probe.exit`; base `3f358dc` | Kit builds; guest/pinned probes and delivery guard return `UINTR=0`/77. No delivery. Prior AWS result is recorded in the hardware runbook, not extrapolated to all cloud hosts. |
| E04 | [Software checks](../results/mac04-software-checks/README.md), `validation.log`, `exit-status`; base `c645f16` | Trap tests: 256 returns and corruption control. Voluntary 32 deferred rounds and million round trips. Both engine tests, drivers and 11 tools tests. Not native UINTR. |
| E05 | [Workflow](../results/mac05-workflow/README.md), `batch.tar.gz`, `analysis/`, `tools/`, binary/environment records in `validation.log`; base `da4dcec` | Eight matched Wait/Cooperative runs, all 7,910 measurement arrivals committed, 432 shorts, 7,478 Q2, 40 handoffs and 160 shorts served during pauses. Per-run counts and all latency columns regenerate. No third matched policy. |
| E07 | [Batching](../results/mac07-batched-notifications/README.md), final logs plus retained `attempt1/`; base `bd557b2` + overlay | Original checkpoint batching checks, partial admissions/drain. Signal completion comes from E09, not this early run. Git-metadata preflight failure retained. |
| E08 | [Starvation](../results/mac08-starvation/README.md), raw pressure batches, recovery script/log, failures; base `2bb11b9` + overlay | Original controlled/checkpoint starvation runs. Recovery rather than full corrected-script repeat; distinct pressure cohorts, not pooled with E09. Signal completion comes from E09. |
| E09 | [Signal](../results/mac09-signal/README.md), `final/`, `final-fixtures/`, `source-base`, `source-changes.tar.gz`, source/binary hashes; base `c798998` + overlay | 358 controlled blocks/2,570 terminal rows; 69 state cases; 1,518 captures/returns, including 1,382 normal arithmetic-interior captures. Nine real Q2 batch cases: 36 writes, 15 captures/resumes. Four budget cases: seven Q2 commits/22 writes. 53 paused real rollback probes. Four pressure runs: all 71,976 shorts = 39,749 commits + 32,227 rejections; Q2 6,577; captures 8,465; total handoffs 14,065; fallback 5,600. 25% forces 7,315 returns/518 admission blocks, 100% zero. Original full pass and focused fixture pass are distinct. |
| E10 | [Notification model](../results/mac10-notification-model/README.md), `final/`, `source-base`, `source-changes.tar.gz`, source/binary hashes; base `6e3a0ba` + overlay | 419 blocks/2,848 rows/822 events/94 schedules including separate signal regressions. Two new Q2 cases: four writes/two voluntary resumes; model paths zero handlers. Six unchanged UINTR-kit hashes. Full corrected validation passed. Not a new pressure run. |

The report's component table intentionally overlaps E09/E10 regressions. Never
add these totals as unique scenarios. Short sample sizes in E05 are 16–37 per
type/run. Report Table 3 is the first one-worker repetition (six rows); the
complete 24-row CSV is included. p99.9 uses nearest rank and is marked low-sample
below 1,000; none of those flags certifies statistical adequacy above the cutoff.
No per-run percentiles are averaged. Throughput excludes drain commits.

## Assessment against the supplied plan

The earlier planning record describes the proposal's intended milestone as a
working interrupt host, dual contexts, tested context-local state and measured
Wait/Cooperative baselines. Contexts, state, workload and matched local smoke now
have evidence. A usable host is still missing. Native I12–I18 acceptance is
unchanged. Local MAC-07–MAC-10 completion is supported by E09/E10, including their
stated window/state/quiescence restrictions; those tasks are not hardware passes.

The user supplied `project_milestone.tex` text in chat on November 5, including
`acmart[sigconf,nonacm]`, group 20, author/email metadata, PVLDB macro definitions
and the four required sections. Those are retained in the report, with a
milestone title and completed content. **Erika Tay** follows this latest supplied
template. The `changes` package uses `commandnameprefix=ifneeded` to resolve its
`comment` collision with acmart. The shared Overleaf URL was inaccessible to
the read tool; no remote project was edited.

The exact original proposal Replication Plan was not supplied. The report marks
all updated plan items and known corrections using `added`/`deleted`; it explicitly
does not claim exhaustive word-level comparison with unseen original text.
Author responsibility assignments come from the shared tracker; member sign-off
of personal contributions is still pending. The separate assignment rubric/page
limit, actual midpoint/final deadlines and group submitter remain unconfirmed.
These are handoff requirements, not invented dates or completed submission.

## MAC-11 actually ran

1. Replayed E09 and E10 in temporary copies, preserving original results;
   regenerated reconciliation JSON byte for byte. All report totals were asserted.
2. Extracted E05 to a temporary directory, ran `analyze.py`, `report.py
   --require-complete`, `check_cooperative_batch.py`, and its independent raw
   metric checker. All four saved CSV/report outputs were byte-identical.
3. Generated three report tables, per-run CSV/JSON and the signal count figure
   directly from replayed facts. Validated current MAC-10 hashes and unchanged
   signal runtime hashes at the frozen revision.
4. Compiled the supplied template with Tectonic 0.17.0 and BibTeX; rendered and
   visually inspected every page. Checked references, required sections, risk
   count, change marking and result traceability.
5. Packaged PDF, editable TeX/bibliography, generated assets, source/evidence and
   pinned dependency, verified file hashes, extracted it to a fresh directory,
   and replayed its evidence. See [verification results](../results/mac11-midpoint/README.md)
   for final actual outcomes.

No fresh C++/engine build, benchmark pressure run, full upstream suite,
teammate-machine run or native interrupt test ran for MAC-11. The first replay
failed only its safe-extraction path check because macOS aliases `/var` and
`/private/var`; it was corrected by resolving the temporary directory. The first
TeX attempt found the acmart/changes command collision; the corrected build passed.
Their logs remain in `results/mac11-midpoint/`.
