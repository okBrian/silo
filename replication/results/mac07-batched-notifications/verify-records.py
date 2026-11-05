#!/usr/bin/env python3
"""Independently reconcile saved CASE blocks against their raw terminal rows."""
import csv
import hashlib
import json
from pathlib import Path


def verify(path):
    lines = path.read_text().splitlines()
    cases = []
    for index, line in enumerate(lines):
        if not line.startswith("CASE "):
            continue
        metadata = line.split()
        name = metadata[1]
        values = dict(item.split("=", 1) for item in metadata[2:])
        header = lines[index + 1]
        assert header.startswith("run_id\t"), (path, line)
        end = index + 2
        while not lines[end].startswith("phase="):
            end += 1
        rows = list(csv.DictReader(lines[index + 1:end], delimiter="\t"))
        summary = dict(item.split("=", 1) for item in lines[end].split())
        ids = sorted(int(row["request_id"]) for row in rows)
        assert ids == list(range(int(summary["generated"]))), (path, name, ids)
        outcomes = {key: sum(row["outcome"] == key for row in rows)
                    for key in ("committed", "rejected_queue_full", "cancelled_shutdown")}
        assert sum(outcomes.values()) == len(rows), (path, name, outcomes)
        assert int(summary["completed"]) == outcomes["committed"]
        assert int(summary["rejected"]) == outcomes["rejected_queue_full"]
        assert int(summary["cancelled"]) == outcomes["cancelled_shutdown"]
        assert summary["failed"] == "0" and summary["dropped"] == "0"
        for row in rows:
            generated, finished = int(row["generated_ns"]), int(row["finished_ns"])
            assert generated <= finished
            if row["admitted_ns"] != "null":
                assert generated <= int(row["admitted_ns"]) <= finished
            if row["first_start_ns"] != "null":
                assert int(row["admitted_ns"]) <= int(row["first_start_ns"]) <= finished
            assert int(row["aborts"]) <= int(row["attempts"])
        high = [row for row in rows if row["priority"] == "high"]
        admitted = sum(row["admitted_ns"] != "null" for row in high)
        commits = sum(row["outcome"] == "committed" for row in high)
        attempts = sum(int(row["attempts"]) for row in high)
        assert admitted == int(values["admitted"])
        if name == "drain-expiry":
            assert len(high) == 4 and commits == attempts == 0
            assert outcomes == {"committed": 1, "rejected_queue_full": 0, "cancelled_shutdown": 5}
        else:
            assert commits == int(values["high_commits"])
            assert attempts == int(values["high_attempts"])
        if "checksum" in values:
            active = next(row for row in rows if row["request_id"] == "0")
            assert active["q2_checksum"] == values["checksum"]
            assert int(values["switches"]) == int(values["low_resumes"])
            assert int(values["real_handlers"]) == 0
        cases.append({"name": name, "high_arrivals": len(high), "high_admitted": admitted,
                      "high_commits": commits, "high_attempts": attempts,
                      "switches": int(values.get("switches", 0)), **outcomes})
    assert cases, path
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "cases": cases}


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    paths = [root / f"batched-{build}.log" for build in ("debug", "optimized", "ubsan")]
    paths += [root / f"repeat-{n}.log" for n in range(1, 21)]
    paths += [root / "tpcc-mixed.log"]
    result = {path.name: verify(path) for path in paths}
    assert all(len(result[path.name]["cases"]) == 11 for path in paths[:-1])
    assert len(result["tpcc-mixed.log"]["cases"]) == 9
    output = root / "reconciliation.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS {len(paths) - 1} standalone suite logs and nine Silo Q2 cases; "
          f"{sum(len(item['cases']) for item in result.values())} CASE blocks reconcile")
