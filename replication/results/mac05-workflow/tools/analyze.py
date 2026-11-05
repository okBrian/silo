#!/usr/bin/env python3
"""Regenerate I05 tables from raw dbtest output and experiment manifests."""

import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import re
import statistics


STOCK_FIELDS = ("throughput_ops_s", "persist_throughput_ops_s", "avg_attempt_latency_ms",
                "avg_persist_latency_ms", "aborts_s")
NUMBER = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[+-]?(?:nan|inf)", re.I)
PERCENTILES = {"p50": 500, "p90": 900, "p99": 990, "p99_9": 999}
TIMESTAMPS = ("scheduled_ns", "generated_ns", "admitted_ns", "first_start_ns", "finished_ns")
OUTCOMES = {
    "committed", "user_aborted", "retry_exhausted", "failed",
    "rejected_queue_full", "cancelled_shutdown",
}
LATENCIES = {
    "queue": ("generated_ns", "first_start_ns"),
    "global_queue": ("generated_ns", "admitted_ns"),
    "local_queue": ("admitted_ns", "first_start_ns"),
    "end_to_end": ("generated_ns", "finished_ns"),
    "scheduled": ("scheduled_ns", "finished_ns"),
}
SUMMARY_FIELDS = {
    "cutoff_ns", "generated", "completed", "rejected", "cancelled", "dropped",
    "cutoff_pending_global", "cutoff_queued", "cutoff_running", "cutoff_due_ungenerated",
    "commits_in_window", "drain_commits", "failed",
}
QUEUED_COUNTS = ("generated", "completed", "rejected", "cancelled", "committed", "user_aborted",
                 "retry_exhausted", "failed_requests", "attempts", "aborts", "commits_in_window",
                 "drain_commits", "drain_completions", "window_throughput_txn_s")


def parse_stock(stdout, stderr=""):
    lines = [line.split() for line in stdout.splitlines()]
    candidates = [fields for fields in lines if len(fields) == 5 and
                  all(NUMBER.fullmatch(field) for field in fields)]
    if len(candidates) != 1:
        raise ValueError(f"expected one five-column dbtest summary, found {len(candidates)}")
    values = [float(field) for field in candidates[0]]
    if any(math.isfinite(value) and value < 0 for value in values):
        raise ValueError("stock summary contains a negative metric")
    metrics = dict(zip(STOCK_FIELDS, (v if math.isfinite(v) else None for v in values)))
    average = metrics["avg_attempt_latency_ms"]
    metrics["avg_attempt_latency_us"] = average * 1000 if average is not None else None
    elapsed = re.search(r"^runtime: ([0-9.]+) sec$", stderr, re.M)
    metrics["reported_runtime_s"] = float(elapsed[1]) if elapsed else None
    return metrics


def phase_summaries(stderr):
    phases = {}
    for line in stderr.splitlines():
        if not line.startswith("phase="):
            continue
        pairs = [field.split("=", 1) for field in line.split()]
        fields = dict(pairs)
        phase = fields.pop("phase")
        if (phase not in {"measurement", "warmup"} or phase in phases or
                len(pairs) != len(fields) + 1 or fields.keys() != SUMMARY_FIELDS):
            raise ValueError(f"invalid or duplicate I06 phase summary for {phase}")
        phases[phase] = {key: int(value) for key, value in fields.items()}
        if any(value < 0 for value in phases[phase].values()) or phases[phase]["failed"] not in (0, 1):
            raise ValueError(f"invalid counts for {phase}")
    return phases


def queued_records(stdout, expected_id):
    reader = csv.DictReader(stdout.splitlines(), delimiter="\t")
    required = {"run_id", "phase", "request_id", "type", "outcome", "attempts", "aborts", *TIMESTAMPS}
    if not required.issubset(reader.fieldnames or []) or len(set(reader.fieldnames)) != len(reader.fieldnames):
        raise ValueError("missing or malformed I06 TSV header")
    run_id = expected_id
    seen = set()
    records = defaultdict(list)
    for row in reader:
        if None in row or any(row[key] is None for key in required):
            raise ValueError(f"truncated or malformed I06 row at line {reader.line_num}")
        if run_id is None:
            run_id = row["run_id"]
        if not run_id or row["run_id"] != run_id:
            raise ValueError(f"unexpected run ID at line {reader.line_num}")
        phase, outcome, kind = row["phase"], row["outcome"], row["type"]
        if phase not in {"measurement", "warmup"} or outcome not in OUTCOMES or not kind:
            raise ValueError(f"invalid phase, outcome or transaction type at line {reader.line_num}")
        checksum = row.get("q2_checksum")
        if kind == "Q2" and outcome == "committed":
            if checksum is None or not checksum.isdecimal() or not 0 <= int(checksum) < 2**64:
                raise ValueError(f"invalid Q2 checksum at line {reader.line_num}")
        elif "q2_checksum" in row and checksum != "null":
            raise ValueError(f"unexpected Q2 checksum at line {reader.line_num}")
        request_id = int(row["request_id"])
        if request_id < 0 or request_id in seen:
            raise ValueError(f"invalid or duplicate request ID {request_id}")
        seen.add(request_id)
        times = {key: None if row[key] in ("null", "") else int(row[key]) for key in TIMESTAMPS}
        present = [value for value in times.values() if value is not None]
        if (times["scheduled_ns"] is None or times["generated_ns"] is None or
                times["finished_ns"] is None or present != sorted(present) or present[0] < 0 or
                (times["first_start_ns"] is not None and times["admitted_ns"] is None) or
                (outcome == "committed" and len(present) != len(TIMESTAMPS))):
            raise ValueError(f"invalid lifecycle timestamps for request {request_id}")
        attempts, aborts = int(row["attempts"]), int(row["aborts"])
        if not 0 <= aborts <= attempts:
            raise ValueError(f"invalid attempt counters for request {request_id}")
        records[phase].append(dict(times, type=kind, outcome=outcome, attempts=attempts, aborts=aborts))
    return run_id, records


def distribution(samples_ns):
    ordered = sorted(samples_ns)
    n = len(ordered)
    values = {f"{key}_us": ordered[(rank * n + 999) // 1000 - 1] / 1000 if n else None
              for key, rank in PERCENTILES.items()}
    return dict(values, n=n, p99_9_low_sample=(n < 1000 if n else None))


def queued_group(records, cutoff, duration):
    outcomes = Counter(row["outcome"] for row in records)
    committed = [row for row in records if row["outcome"] == "committed"]
    in_window = sum(row["finished_ns"] < cutoff for row in committed) if cutoff is not None else None
    rejected = outcomes["rejected_queue_full"]
    cancelled = outcomes["cancelled_shutdown"]
    drain_completions = None if cutoff is None else sum(
        row["finished_ns"] >= cutoff and row["outcome"] not in ("rejected_queue_full", "cancelled_shutdown")
        for row in records
    )
    return {
        "generated": len(records), "completed": len(records) - rejected - cancelled,
        "rejected": rejected, "cancelled": cancelled, "committed": len(committed),
        "user_aborted": outcomes["user_aborted"], "retry_exhausted": outcomes["retry_exhausted"],
        "failed_requests": outcomes["failed"], "attempts": sum(row["attempts"] for row in records),
        "aborts": sum(row["aborts"] for row in records),
        "commits_in_window": in_window,
        "drain_commits": len(committed) - in_window if in_window is not None else None,
        "drain_completions": drain_completions,
        "window_throughput_txn_s": in_window / duration if in_window is not None and duration else None,
        "latency": {name: distribution([row[end] - row[start] for row in committed])
                    for name, (start, end) in LATENCIES.items()},
    }


def cutoff_counts(records, cutoff):
    counts = Counter()
    for row in records:
        if row["generated_ns"] >= cutoff:
            counts["cutoff_due_ungenerated"] += 1
        elif row["finished_ns"] >= cutoff:
            admitted, first_start = row["admitted_ns"], row["first_start_ns"]
            if admitted is None or admitted >= cutoff:
                counts["cutoff_pending_global"] += 1
            elif first_start is None or first_start >= cutoff:
                counts["cutoff_queued"] += 1
            else:
                counts["cutoff_running"] += 1
    return counts


def parse_queued(stdout, stderr, expected_id=None, durations=None):
    phases = phase_summaries(stderr)
    run_id, records = queued_records(stdout, expected_id)
    if not records and not phases:
        raise ValueError("I06 TSV has no terminal records or phase summaries")
    groups = []
    errors = []
    for phase in ("warmup", "measurement"):
        if phase not in records and phase not in phases:
            if phase == "measurement" or (durations and durations.get(phase)):
                errors.append(f"missing {phase} phase")
            continue
        summary = phases.get(phase)
        cutoff = summary["cutoff_ns"] if summary else None
        duration = durations.get(phase) if durations else None
        rows = records[phase]
        overall = queued_group(rows, cutoff, duration)
        counts = ("generated", "completed", "rejected", "cancelled", "commits_in_window", "drain_commits")
        reconciled = (summary is not None and not summary["dropped"] and
                      all(summary[key] == overall[key] for key in counts))
        if reconciled:
            outstanding = cutoff_counts(rows, cutoff)
            reconciled = all(summary[key] == outstanding[key] for key in SUMMARY_FIELDS
                             if key.startswith("cutoff_") and key != "cutoff_ns")
        if not reconciled:
            errors.append(f"{phase} lacks a matching I06 phase summary")
        # A mismatched summary means the observed TSV can be incomplete. The
        # cutoff and aggregate counts from stderr remain available in JSON.
        if summary and not reconciled:
            continue
        by_type = defaultdict(list)
        for row in rows:
            by_type[row["type"]].append(row)
        groups.append(dict(overall, phase=phase, type="All", reconciled=reconciled))
        for kind, members in sorted(by_type.items()):
            groups.append(dict(queued_group(members, cutoff, duration), phase=phase,
                               type=kind, reconciled=reconciled))
    return {"observed_run_id": run_id, "phases": phases, "groups": groups, "issues": errors}


def read_optional(path):
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def analyze_run(manifest, stdout, stderr, archived_success=False):
    run = dict(manifest, stdout=str(stdout), stderr=str(stderr))
    run.update(stock=None, queued=None, issues=[])
    state = manifest.get("state")
    status = "incomplete"
    if state in ("timed_out", "launch_failed", "interrupted") or manifest.get("exit_status") not in (None, 0):
        status = "failed"
    elif manifest.get("exit_status") == 0 or archived_success:
        status = "complete"
    elif state == "archived":
        status = "unverified"
    try:
        if not stdout.is_file():
            raise ValueError("stdout log is missing")
        raw, diagnostics = read_optional(stdout), read_optional(stderr)
        if manifest["format"] == "stock":
            run["stock"] = parse_stock(raw, diagnostics)
        else:
            queued = parse_queued(raw, diagnostics, manifest.get("run_id"), manifest.get("phases_s"))
            run["queued"] = queued
            run["run_id"] = manifest.get("run_id") or queued["observed_run_id"] or stdout.stem
            run["issues"].extend(queued["issues"])
            if any(summary["failed"] for summary in queued["phases"].values()):
                status = "failed"
            elif status == "unverified" and not run["issues"]:
                status = "phase-verified"
    except (ValueError, OSError, csv.Error) as error:
        run["issues"].append(str(error))
    run["status"] = "incomplete" if run["issues"] and status != "failed" else status
    if not run.get("run_id"):
        run["run_id"] = stdout.stem
    return run


def load_batch(directory):
    metadata = json.loads((directory / "batch.json").read_text(encoding="utf-8"))
    runs = []
    for run_id in metadata["run_ids"]:
        path = directory / run_id
        manifest = json.loads((path / "manifest.json").read_text(encoding="utf-8"))
        runs.append(analyze_run(manifest, path / "stdout.log", path / "stderr.log"))
    return {"batch": metadata, "runs": runs}


def load_pace(directory):
    environment = (directory / "environment.txt").read_text(encoding="utf-8")
    match = re.search(r"^job=(\S+) cpus_per_task=\d+ repeats=(\d+) runtime_s=(\d+) scale_factor=(\d+)$",
                      environment, re.M)
    if not match:
        raise ValueError("legacy PACE environment is missing its run settings")
    job_id, repeats, runtime, warehouses = match.groups()
    job_trace = set(read_optional(directory / "job.log").splitlines())
    exit_status = read_optional(directory / "exit-status").strip()
    runs = []
    for repeat in range(1, int(repeats) + 1):
        for workers in ((1, 2) if repeat % 2 else (2, 1)):
            name = f"tpcc-w{workers}-r{repeat}"
            manifest = {
                "run_id": f"pace-{job_id}-{name}", "format": "stock", "state": "archived",
                "config": "stock-tpcc-2wh", "workers": workers, "variant": workers, "repeat": repeat,
                "warehouses": int(warehouses), "phases_s": {"warmup": 0, "measurement": int(runtime),
                                                           "drain_limit": 0},
                "exit_status": None,
            }
            # Under set -e this command was reachable only after dbtest succeeded.
            trace = f"+ awk -v w={workers} -v r={repeat} -v t={runtime} '"
            runs.append(analyze_run(manifest, directory / f"{name}.stdout", directory / f"{name}.stderr",
                                    archived_success=trace in job_trace))
    return {"batch": {"slurm_job_id": job_id, "job_exit_status": int(exit_status) if exit_status else None,
                      "environment": str(directory / "environment.txt"),
                      "job_log": str(directory / "job.log")}, "runs": runs}


def load_queued(paths, duration):
    return {"batch": None, "runs": [
        analyze_run({"format": "queued", "state": "archived", "exit_status": None,
                     "phases_s": {"measurement": duration}}, path, path.with_suffix(".err"))
        for path in paths
    ]}


def stock_table(runs):
    groups = defaultdict(list)
    for run in runs:
        if run["format"] == "stock":
            key = (run["config"], run["warehouses"], run["phases_s"]["measurement"],
                   run["workers"], run["variant"])
            groups[key].append(run)
    for key, group in sorted(groups.items()):
        config, warehouses, runtime, workers, variant = key
        complete = [run["stock"] for run in group if run["status"] == "complete"]
        rates = [row["throughput_ops_s"] for row in complete if row["throughput_ops_s"] is not None]
        latencies = [row["avg_attempt_latency_us"] for row in complete if row["avg_attempt_latency_us"] is not None]
        aborts = [row["aborts_s"] for row in complete if row["aborts_s"] is not None]
        yield {
            "config": config, "warehouses": warehouses, "runtime_s": runtime, "workers": workers, "variant": variant,
            "runs": len(complete), "planned": len(group),
            "throughput_n": len(rates), "attempt_latency_run_n": len(latencies), "aborts_run_n": len(aborts),
            "mean_txn_s": statistics.mean(rates) if rates else None,
            "sd_txn_s": statistics.stdev(rates) if len(rates) > 1 else None,
            "min_txn_s": min(rates) if rates else None, "max_txn_s": max(rates) if rates else None,
            "mean_attempt_latency_us": statistics.mean(latencies) if latencies else None,
            "mean_aborts_s": statistics.mean(aborts) if aborts else None,
        }


def queued_table(runs):
    for run in runs:
        if run["format"] != "queued":
            continue
        common = {"run_id": run["run_id"], "status": run["status"], "exit_status": run["exit_status"]}
        groups = run["queued"]["groups"] if run["queued"] else []
        if not any(group["phase"] == "measurement" for group in groups):
            yield dict(common, phase="measurement", type="All")
        for group in groups:
            row = {key: value for key, value in group.items() if key != "latency"}
            for name, stats in group["latency"].items():
                row.update({f"{name}_{key}": value for key, value in stats.items()})
            yield dict(common, **row)


def csv_table(path, rows, fields):
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def value(number, format_spec=","):
    return format(number, format_spec) if number is not None else ""


def markdown_table(headers, rows):
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(lines) + "\n"


def stock_markdown(rows):
    display = []
    for row in rows:
        mean = value(row["mean_txn_s"], ",.0f")
        if row["sd_txn_s"] is not None:
            mean += f" ± {row['sd_txn_s']:,.0f}"
        limits = f"{row['min_txn_s']:,.0f}–{row['max_txn_s']:,.0f}" if row["throughput_n"] else ""
        display.append([row["config"], str(row["warehouses"]), str(row["runtime_s"]), str(row["workers"]),
                        str(row["variant"]), str(row["runs"]), str(row["planned"]), mean, limits,
                        value(row["mean_attempt_latency_us"], ".2f"), value(row["mean_aborts_s"], ".2f")])
    headers = ["Configuration", "Warehouses", "Seconds", "Workers", "Variant", "Runs", "Planned",
               "Throughput mean ± sample SD (txn/s)", "Throughput range (txn/s)",
               "Average of timed successful attempts (µs)", "Mean aborts/s"]
    return ("# Stock TPC-C\n\nEach latency cell averages the reported run means with equal weight.\n\n"
            + markdown_table(headers, display))


def queued_markdown(rows):
    def latency(row, metric):
        n = row.get(f"{metric}_n")
        if n is None:
            return ""
        if n == 0:
            return "0"
        cells = [value(row[f"{metric}_{rank}_us"], ",.3f") for rank in PERCENTILES]
        if row[f"{metric}_p99_9_low_sample"]:
            cells[-1] += "†"
        return f"{n:,} / " + " / ".join(cells)

    display = []
    for row in rows:
        counts = [value(row.get(key)) for key in ("generated", "committed", "rejected", "cancelled",
                                                  "commits_in_window", "drain_completions", "drain_commits")]
        display.append([row["run_id"].replace("|", "\\|"), row["status"], row["phase"], row["type"], *counts,
                        latency(row, "queue"), latency(row, "end_to_end")])
    headers = ["Run", "Status", "Phase", "Type", "Generated", "Committed", "Rejected", "Cancelled",
               "Window commits", "Drain completions", "Drain commits", "Queue delay (µs)", "End-to-end latency (µs)"]
    return ("# Queued TPC-C\n\nLatency cells show n / p50 / p90 / p99 / p99.9. "
            "† p99.9 uses fewer than 1,000 samples.\n\n" + markdown_table(headers, display))


def write_results(result, output):
    output.mkdir(parents=True, exist_ok=True)
    runs = result["runs"]
    stock = list(stock_table(runs))
    queued = list(queued_table(runs))
    result.update(stock_summary=stock, queued_summary=queued)
    (output / "analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    if stock:
        fields = ["run_id", "status", "exit_status", "workers", "repeat", "variant", "runtime_s", "warehouses",
                  *STOCK_FIELDS, "avg_attempt_latency_us", "reported_runtime_s"]
        rows = []
        for run in runs:
            if run["format"] == "stock":
                row = {key: run[key] for key in fields if key in run}
                rows.append(dict(row, **(run["stock"] or {}), runtime_s=run["phases_s"]["measurement"]))
        csv_table(output / "stock-runs.csv", rows, fields)
        csv_table(output / "stock-summary.csv", stock, list(stock[0]))
        (output / "stock-summary.md").write_text(stock_markdown(stock), encoding="utf-8")
    if queued:
        fields = ["run_id", "status", "exit_status", "phase", "type", "reconciled", *QUEUED_COUNTS]
        for metric in LATENCIES:
            fields.extend(f"{metric}_{key}" for key in ("n", *(f"{p}_us" for p in PERCENTILES),
                                                       "p99_9_low_sample"))
        csv_table(output / "queued-summary.csv", queued, fields)
        (output / "queued-summary.md").write_text(queued_markdown(queued), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    inputs = parser.add_subparsers(dest="format", required=True)
    inputs.add_parser("batch").add_argument("directory", type=Path)
    inputs.add_parser("pace").add_argument("directory", type=Path)
    queued = inputs.add_parser("queued")
    queued.add_argument("tsv", type=Path, nargs="+", help="archived TSVs with adjacent .err files")
    queued.add_argument("--measurement-s", type=int, help="known measurement duration")
    args = parser.parse_args()
    try:
        if args.format == "batch":
            result = load_batch(args.directory)
        elif args.format == "pace":
            result = load_pace(args.directory)
        else:
            if args.measurement_s is not None and args.measurement_s <= 0:
                raise ValueError("--measurement-s must be positive")
            result = load_queued(args.tsv, args.measurement_s)
        write_results(result, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()
