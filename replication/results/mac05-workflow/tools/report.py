#!/usr/bin/env python3
"""Write a per-run report from I05 analysis; retain failed and incomplete runs."""

import argparse
import csv
import json
from pathlib import Path

import analyze


def report(analysis, output, label):
    output.mkdir(parents=True, exist_ok=True)
    runs = [run for run in analysis["runs"] if run["format"] == "queued"]
    records = []
    health = []
    for run in runs:
        queued = run.get("queued")
        groups = [group for group in queued["groups"] if group["phase"] == "measurement"] if queued else []
        overall = next((group for group in groups if group["type"] == "All"), None)
        verified = overall is not None and overall["reconciled"]
        summary = queued["phases"].get("measurement") if queued else None
        # A partial trace cannot establish how many requests are missing at exit.
        missing = (summary["generated"] - overall["generated"]) if verified else None
        outstanding = sum(summary[key] for key in (
            "cutoff_pending_global", "cutoff_queued", "cutoff_running", "cutoff_due_ungenerated"
        )) if verified else None
        health.append({
            "run_id": run["run_id"], "policy": run.get("policy"), "workers": run.get("workers"),
            "repeat": run.get("repeat"), "status": run["status"],
            "reconciled": verified, "outstanding_at_cutoff": outstanding,
            "unaccounted_at_exit": missing, "issues": "; ".join(run["issues"]),
        })
        for group in groups:
            if group["type"] == "All":
                continue
            row = {key: health[-1][key] for key in ("run_id", "policy", "workers", "repeat", "status")}
            row.update(type=group["type"], reconciled=group["reconciled"])
            row.update({key: group[key] for key in analyze.QUEUED_COUNTS})
            row["not_committed"] = group["generated"] - group["committed"]
            for metric, stats in group["latency"].items():
                row.update({f"{metric}_{key}": value for key, value in stats.items()})
            records.append(row)
    health_fields = ["run_id", "policy", "workers", "repeat", "status", "reconciled",
                     "outstanding_at_cutoff", "unaccounted_at_exit", "issues"]
    analyze.csv_table(output / "run-health.csv", health, health_fields)
    fields = ["run_id", "policy", "workers", "repeat", "status", "type", "reconciled",
              *analyze.QUEUED_COUNTS, "not_committed"]
    for metric in analyze.LATENCIES:
        fields.extend(f"{metric}_{key}" for key in ("n", *(f"{p}_us" for p in analyze.PERCENTILES),
                                                   "p99_9_low_sample"))
    analyze.csv_table(output / "per-type.csv", records, fields)

    def cell(value):
        return "unknown" if value is None else str(value).replace("|", "\\|").replace("\n", " ")

    def latency(row, metric):
        values = [row[f"{metric}_{rank}_us"] for rank in analyze.PERCENTILES]
        return " / ".join("null" if value is None else f"{value:.3f}" for value in values)

    lines = ["# Experiment collection report\n", label + "\n",
             "Measurement arrival cohorts; each row is one repetition, with no pooling. "
             "Latencies are committed requests including drain commits, in microseconds, "
             "using nearest rank. Throughput counts only commits before cutoff. "
             "The CSV also retains global/local queue and scheduled latency, all outcome "
             "counts, attempts, aborts, and sample counts.\n",
             "Outstanding at cutoff includes pending global, queued, running/suspended, "
             "and due-but-not-generated requests. Unaccounted at exit is known only for "
             "a reconciled terminal trace. It excludes recorded cancellation/rejection; "
             "these remain separate outcomes. Unknown is never zero.\n",
             analyze.markdown_table(health_fields, [[cell(row[key]) for key in health_fields] for row in health]),
             analyze.markdown_table(
                 ["Run", "Policy", "Workers", "Type", "Status", "n", "Latency p50/p90/p99/p99.9 (µs)",
                  "Queue p50/p90/p99/p99.9 (µs)", "Window txn/s", "Aborts", "Not committed", "p99.9 <1,000"],
                 [[row["run_id"], row["policy"] or "unknown", cell(row["workers"]), row["type"],
                   row["status"], cell(row["end_to_end_n"]), latency(row, "end_to_end"), latency(row, "queue"),
                   cell(row["window_throughput_txn_s"]), cell(row["aborts"]), cell(row["not_committed"]),
                   cell(row["end_to_end_p99_9_low_sample"])] for row in records]),
             "Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.\n"]
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")
    return bool(runs) and all(row["status"] in ("complete", "phase-verified") and row["reconciled"]
                              and row["unaccounted_at_exit"] == 0 for row in health)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("analysis", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--label", required=True, help="environment and smoke/performance qualification")
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    complete = report(json.loads(args.analysis.read_text(encoding="utf-8")), args.output, args.label)
    return int(args.require_complete and not complete)


if __name__ == "__main__":
    raise SystemExit(main())
