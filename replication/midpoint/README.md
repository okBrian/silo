# Local midpoint package

The report uses the `project_milestone.tex` course template supplied in chat,
with the author metadata and four required sections retained. It presents
**local functional evidence**, not a native UINTR replication. The frozen
source/evidence index is [EVIDENCE.md](EVIDENCE.md).

## Handoff still required

- Confirm midpoint and final Canvas deadlines and group submitter. November 6
  and November 8 in the report are proposed review/access checkpoints, not
  course deadlines. Map the final-minus-7/3-day gates to the actual deadline.
- Check the actual rubric/page limit, verify original-proposal plan wording
  and change markings, and obtain individual contribution sign-off.
- Review names: the supplied template spells **Erika Tay**, which this report
  preserves, superseding the older planning document's spelling.
- Submission and remote Overleaf edits were not performed.

## Reproduce analysis without Linux

From the extracted package's `silo/` directory, with Python 3.9 or newer:

```bash
python3 -B replication/midpoint/replay.py --output /tmp/midpoint-replay
```

This uses only the standard library. It copies the saved signal/model evidence
to a temporary directory, replays raw records, verifies all reported aggregate
counts, regenerates the original smoke analysis and compares original reports.
It never imports/runs the engine or modifies historical evidence.

Generate report inputs with Python + ReportLab:

```bash
python3 -B replication/midpoint/replay.py
python3 -B replication/midpoint/make_results.py
```

`generated/facts.json`, `smoke.csv`, `pressure.csv`, `per-type.csv`, three TeX
tables, and `pressure-outcomes.{pdf,svg}` are the editable/generated inputs.
Only the first one-worker repetition is shown in the report's latency table;
all runs/types/percentiles/outcomes remain in the CSV.

## Rebuild the PDF

Use Tectonic 0.17.0 (tested) or a TeX Live installation with `acmart`, `changes`,
`graphicx`, `booktabs`, BibTeX and the ACM reference style. From `silo/`:

```bash
mkdir -p output/pdf
cd replication/midpoint
tectonic --untrusted --keep-logs --outdir ../../output/pdf project_milestone.tex
```

Alternatively, run `latexmk -pdf project_milestone.tex` in this directory.
Tectonic downloads its TeX bundle on the first build; no compiler binary is
bundled. The tested acmart class is v1.83 (2022/02/19), selected by Tectonic's
bundle, not an asserted latest version. The TeX package sources are trusted
standard dependencies; shell escape is disabled by `--untrusted`.

For visual review with Poppler, render every page:

```bash
pdftoppm -r 120 -png output/pdf/project_milestone.pdf /tmp/midpoint-page
```

## Source and engine reruns

The ZIP's `silo/` is a full source/evidence snapshot at the frozen revision with
the MAC-11 documentation/report overlay. Its `masstree/` contains the pinned
dependency source, including its license. `inventory.json` hashes original
MAC-01–MAC-10 evidence; `PACKAGE-SHA256.json` hashes every packaged regular file.
Git history and executables are not included. Original source revisions, tested
diffs, binary hashes, environment logs, commands and failures are included.

Fresh engine validation requires x86-64 Linux/glibc and the prerequisites in
`replication/experiments/HOST_WORKFLOW.md`. Use a disposable source copy. The
validation scripts inspect Git metadata, so initialize the extracted Masstree
and source as separate local repositories and commit their snapshot contents
before using those scripts. Record those newly created revisions as restored
snapshot metadata, not the original historical commits. Confirm source hashes
against the freeze before applying the archived GCC compatibility patch. Then
use `replication/tests/validate_notification_model.sh` and the separate signal
validation/pressure recipes in the evidence READMEs. Do not apply the GCC patch
twice: the scripts already apply it when documented. No fresh engine rerun of
this restored snapshot is claimed by MAC-11.

Native UINTR work uses `replication/uintr/README.md` and must pass qualification
and delivery before integration/performance collection. The Mac model/signal
data cannot satisfy native I12–I18 acceptance.

To repackage from a Git checkout, use `package.py` with the pinned dependency
archive as documented in its help. The ZIP output defaults to
`output/midpoint/local-midpoint-package.zip`.
