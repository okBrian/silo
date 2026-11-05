#!/usr/bin/env python3
"""Replay frozen midpoint evidence without running an engine or modifying old logs."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "replication/results"


def run(command, log):
    result = subprocess.run([str(x) for x in command], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    with log.open("a") as stream:
        stream.write("$ " + " ".join(map(str, command)) + "\n" + result.stdout)
        stream.write(f"exit={result.returncode}\n")
    if result.returncode:
        raise RuntimeError(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "replication/midpoint/generated")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "replay.log"
    log.write_text("MAC-11: analysis replay only; no fresh engine/hardware tests.\n")
    py = [sys.executable, "-B"]
    with tempfile.TemporaryDirectory(prefix="mac11-replay-") as scratch:
        scratch = Path(scratch).resolve()
        # Existing verifiers write JSON: run against copies and compare to the freeze.
        for name in ("mac09-signal", "mac10-notification-model"):
            shutil.copytree(RESULTS / name, scratch / name)
        signal = scratch / "mac09-signal"
        model = scratch / "mac10-notification-model"
        run([*py, "replication/tests/verify_signal_records.py", signal / "final",
             "--fixtures", signal / "final-fixtures"], log)
        run([*py, "replication/tests/verify_notification_model.py", model / "final"], log)
        for name in ("mac09-signal", "mac10-notification-model"):
            relative = Path(name) / "final/reconciliation.json"
            assert (scratch / relative).read_bytes() == (RESULTS / relative).read_bytes(), relative
        s = json.loads((signal / "final/reconciliation.json").read_text())
        m = json.loads((model / "final/reconciliation.json").read_text())
        # The archived batch is a trusted local input; validate extraction paths anyway.
        with tarfile.open(RESULTS / "mac05-workflow/batch.tar.gz") as archive:
            for member in archive.getmembers():
                assert not member.issym() and not member.islnk()
                assert (scratch / member.name).resolve().is_relative_to(scratch)
            archive.extractall(scratch)
        batch, = scratch.glob("queued-cooperative-smoke-*")
        analysis = scratch / "analysis"
        run([*py, "replication/experiments/analyze.py", "--output", analysis, "batch", batch], log)
        saved = RESULTS / "mac05-workflow/analysis/report.md"
        label = saved.read_text().splitlines()[2]
        run([*py, "replication/experiments/report.py", analysis / "analysis.json", "--output",
             analysis, "--label", label, "--require-complete"], log)
        run([*py, "replication/tests/check_cooperative_batch.py", batch], log)
        run([*py, "replication/results/mac05-workflow/verify-replay.py", batch, analysis], log)
        for name in ("queued-summary.csv", "per-type.csv", "run-health.csv", "report.md"):
            assert (analysis / name).read_bytes() == (saved.parent / name).read_bytes(), name
        smoke = []
        for path in sorted(batch.glob("*/manifest.json")):
            manifest = json.loads(path.read_text())
            with (path.parent / "stdout.log").open() as stream:
                rows = [r for r in csv.DictReader(stream, delimiter="\t") if r["phase"] == "measurement"]
            summary, = [dict(token.split("=", 1) for token in line.split())
                        for line in (path.parent / "stderr.log").read_text().splitlines()
                        if line.startswith("phase=measurement ")]
            cutoff = int(summary["cutoff_ns"])
            smoke.append(dict(run=path.parent.name, policy=manifest["policy"], workers=manifest["workers"],
                              repeat=manifest["repeat"], commits=len(rows),
                              shorts=sum(r["priority"] == "high" for r in rows),
                              q2=sum(r["type"] == "Q2" for r in rows),
                              window=sum(int(r["finished_ns"]) < cutoff for r in rows),
                              drain=sum(int(r["finished_ns"]) >= cutoff for r in rows),
                              handoffs=sum(int(r["cooperative_yields"]) for r in rows)))
        per_type = list(csv.DictReader((analysis / "per-type.csv").open()))
        assert sum(r["commits"] for r in smoke) == 7910
        assert sum(r["handoffs"] for r in smoke) == 40
        assert m["totals"] == dict(cases=419, records=2848, events=822, schedules=94)
        pressure = s["pressure"]
        assert sum(r["q2_commits"] for r in pressure) == 6577
        assert sum(r["short_commits"] for r in pressure) == 39749
        assert sum(r["rejected"] for r in pressure) == 32227
        assert sum(r["async_captures"] for r in pressure) == 8465
        assert sum(r["forced_returns"] for r in pressure) == 7315
        assert sum(r["admission_blocks"] for r in pressure) == 518
        assert sum(int(n["switches"]) for r in pressure for n in r["signal"]) == 14065
        assert sum(int(r["captures"]) for r in s["standalone"]) == 1518
        assert sum(int(r["interior_captures"]) for r in s["standalone"] if r["corrupt"] == "0") == 1382
        assert sum(len(r["cases"]) for r in s["controlled"]) == 358
        assert sum(c["records"] for r in s["controlled"] for c in r["cases"]) == 2570
        assert len(s["standalone"]) == 69
        facts = dict(frozen_revision="7319ba60394b6d68a4d8f6382177c5e5758c6b28",
                     smoke=smoke, per_type=per_type, pressure=pressure, model=m["totals"],
                     signal_cases=sum(len(r["cases"]) for r in s["controlled"]),
                     signal_records=sum(c["records"] for r in s["controlled"] for c in r["cases"]),
                     signal_state_checks=len(s["standalone"]))
        (out / "facts.json").write_text(json.dumps(facts, indent=2) + "\n")
        for name, rows in (("smoke.csv", smoke), ("pressure.csv", [{k: v for k, v in r.items() if k != "signal"} for r in pressure])):
            with (out / name).open("w") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        shutil.copy2(analysis / "per-type.csv", out / "per-type.csv")
    with log.open("a") as stream:
        stream.write("PASS: saved JSON, all four MAC-05 report files, and report numeric assertions match.\n")
    print("PASS MAC-11 frozen evidence replay; facts/tables written to", out)


if __name__ == "__main__":
    main()
