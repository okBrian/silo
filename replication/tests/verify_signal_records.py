#!/usr/bin/env python3
"""Independently reconcile MAC-09's terminal records and signal counters."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path


def fields(line):
    return dict(token.split("=", 1) for token in line.split() if "=" in token)


def rows_check(rows):
    assert sorted(int(r["request_id"]) for r in rows) == list(range(len(rows)))
    for r in rows:
        assert r["outcome"] != "pending"
        assert int(r["generated_ns"]) <= int(r["finished_ns"])
        if r["outcome"] == "committed":
            assert int(r["generated_ns"]) <= int(r["admitted_ns"]) <= int(r["first_start_ns"]) <= int(r["finished_ns"])
            assert int(r["attempts"]) == int(r["aborts"]) + 1


def controlled(path):
    lines = path.read_text().splitlines()
    result = []
    for i, line in enumerate(lines):
        if not line.startswith("CASE "):
            continue
        meta = fields(line)
        end = next(j for j in range(i + 2, len(lines)) if lines[j].startswith("phase="))
        rows = list(csv.DictReader(io.StringIO("\n".join(lines[i + 1:end])), delimiter="\t"))
        rows_check(rows)
        summary = fields(lines[end])
        assert summary["failed"] == summary["dropped"] == "0"
        assert len(rows) == int(summary["generated"])
        for outcome, key in (("committed", "completed"), ("rejected_queue_full", "rejected"), ("cancelled_shutdown", "cancelled")):
            assert sum(r["outcome"] == outcome for r in rows) == int(summary[key])
        high = [r for r in rows if r["priority"] == "high"]
        if "high_commits" in meta:
            assert sum(r["outcome"] == "committed" for r in high) == int(meta["high_commits"])
        if "high_attempts" in meta:
            assert sum(int(r["attempts"]) for r in high) == int(meta["high_attempts"])
        if "checksum" in meta:
            assert next(r for r in rows if r["request_id"] == "0")["q2_checksum"] == meta["checksum"]
            assert int(meta["async_captures"]) > 0
            assert int(meta["real_handlers"]) >= int(meta["async_captures"])
        if "expire" in meta:
            assert meta["forced_returns"] == "5" and meta["high_service_ns"] == "60"
            assert meta["retry_retained"] == meta["exact_threshold_dispatch"] == "1"
            assert int(summary["cancelled"]) == (3 if meta["expire"] == "1" else 0)
        result.append({"case": line.split()[1], "records": len(rows), **meta})
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--fixtures", type=Path, help="final focused fixture evidence; runtime/state/pressure stay in evidence")
    args = parser.parse_args()
    root = args.evidence
    fixtures = args.fixtures or root
    result = {"controlled": [], "standalone": [], "pressure": []}
    builds = ("debug", "optimized", "ubsan")
    for suite, count in (("batched_notifications", 11), ("starvation", 4)):
        paths = [fixtures / f"{suite}-{b}.log" for b in builds]
        paths += [fixtures / f"{suite}-repeat-{n}.log" for n in range(1, 21)]
        for path in paths:
            cases = controlled(path)
            assert len(cases) == count, (path, len(cases))
            result["controlled"].append({"log": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "cases": cases})
    engine_path = fixtures / "tpcc-signal-aborts.log"
    if not engine_path.exists():
        engine_path = fixtures / "tpcc-signal.log"
    assert "PASS 53 actual Silo failed-write rollback/cleanup probes" in engine_path.read_text()
    engine = controlled(engine_path)
    assert len(engine) == 13
    result["controlled"].append({"log": engine_path.name, "cases": engine})
    for path in [*(root / f"signal-{b}.log" for b in builds), *(root / f"signal-repeat-{n}.log" for n in range(1, 21))]:
        lines = path.read_text().splitlines()
        assert len(lines) == 3
        for line, corrupt in zip(lines, (0, 1, 2)):
            meta = fields(line)
            assert line.startswith("PASS linux-signal") and int(meta["corrupt"]) == corrupt
            assert meta["captures"] == meta["switches"] == ("1" if corrupt else "64")
            assert meta["cleanup"] == "1" and meta["exact_continuation"] == ("0" if corrupt else "1")
            if not corrupt:
                assert int(meta["interior_captures"]) > 0
            result["standalone"].append({"log": path.name, **meta})
    for threshold in (25, 100):
        paths = sorted((root / f"pressure-{threshold}").glob("*/*/manifest.json"))
        assert len(paths) == 2
        for path in paths:
            manifest = json.loads(path.read_text())
            assert manifest["state"] == "finished" and manifest["exit_status"] == 0
            assert manifest["policy"] == "linux-signal" and manifest["starvation_threshold"] == threshold
            assert manifest["starvation_clock"] == "CLOCK_MONOTONIC nanoseconds"
            rows = list(csv.DictReader((path.parent / "stdout.log").open(), delimiter="\t"))
            rows_check(rows)
            lows = [r for r in rows if r["type"] == "Q2" and r["outcome"] == "committed"]
            high = [r for r in rows if r["priority"] == "high"]
            assert lows and all(r["q2_checksum"] != "null" and int(r["low_service_ns"]) > 0 for r in lows)
            assert len(high) == 2999 * 4 * manifest["workers"]
            assert all(r["outcome"] in ("committed", "rejected_queue_full", "cancelled_shutdown") for r in rows)
            stderr = (path.parent / "stderr.log").read_text().splitlines()
            budget = [fields(l) for l in stderr if l.startswith("starvation phase=measurement")]
            signal = [fields(l) for l in stderr if l.startswith("notifications phase=measurement")]
            captures = sum(int(s["async_captures"]) for s in signal)
            forced = sum(int(s["forced_low_returns"]) for s in budget)
            blocks = sum(int(s["admission_blocks"]) for s in budget)
            assert captures > 0 and (forced > 0 if threshold == 25 else forced == blocks == 0)
            result["pressure"].append({"run": path.parent.name, "threshold": threshold, "workers": manifest["workers"],
                "q2_commits": len(lows), "short_arrivals": len(high), "short_commits": sum(r["outcome"] == "committed" for r in high),
                "rejected": sum(r["outcome"] == "rejected_queue_full" for r in high), "cancelled": sum(r["outcome"] == "cancelled_shutdown" for r in rows),
                "short_aborts": sum(int(r["aborts"]) for r in high), "async_captures": captures, "forced_returns": forced,
                "admission_blocks": blocks, "low_service_ns": sum(int(r["low_service_ns"]) for r in lows), "signal": signal})
    (root / "reconciliation.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS", sum(len(r["cases"]) for r in result["controlled"]), "controlled CASE blocks;",
          sum(c["records"] for r in result["controlled"] for c in r["cases"]), "terminal records;",
          len(result["standalone"]), "state checks; four signal pressure runs")
    print(json.dumps(result["pressure"], indent=2))


if __name__ == "__main__":
    main()
