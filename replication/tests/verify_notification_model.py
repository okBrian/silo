#!/usr/bin/env python3
"""Replay MAC-10 event traces and reconcile independent terminal records."""
import csv
import io
import json
import sys
from pathlib import Path


def fields(line):
    return dict(word.split("=", 1) for word in line.split() if "=" in word)


def replay(events):
    slots = {}
    previous = 0
    for label, values in events:
        e = {k: int(v) for k, v in values.items()}
        assert e["at"] >= previous
        previous = e["at"]
        s = slots.setdefault(e["worker"], dict.fromkeys(("sends", "coalesced", "deliveries", "unsolicited", "pending", "closed", "delivery_attempts", "dropped_sends", "dropped_deliveries", "cancelled"), 0))
        delivered = 0
        if e["kind"] == 0:
            s["sends"] += 1
            if s["closed"]:
                s["dropped_sends"] += 1
            else:
                s["coalesced"] += s["pending"]
                s["pending"] = 1
        elif e["kind"] in (1, 2):
            s["delivery_attempts"] += 1
            if s["closed"]:
                s["dropped_deliveries"] += 1
            elif s["pending"] or e["kind"] == 2:
                s["unsolicited"] += 1 - s["pending"]
                s["pending"] = 0
                s["deliveries"] += 1
                delivered = 1
                if label == "rearm" and s["deliveries"] == 1:
                    s["sends"] += 1  # Explicit fixture receiver callback sends again.
                    s["pending"] = 1
        else:
            assert e["kind"] == 3
            s["closed"] = 1
            s["cancelled"] += s["pending"]
            s["pending"] = 0
        assert e["delivered"] == delivered, e
        assert all(e[k] == v for k, v in s.items()), (label, e, s)


def inspect(path, expected_cases, signal=False):
    lines = path.read_text().splitlines()
    cases = records = event_count = groups = 0
    events = []
    for i, line in enumerate(lines + [""]):
        if line.startswith("EVENT "):
            events.append((line.split()[1], fields(line)))
            event_count += 1
            continue
        if events:
            replay(events)
            groups += 1
            events = []
        if not line.startswith("CASE "):
            continue
        cases += 1
        meta = fields(line)
        end = next(j for j in range(i + 2, len(lines)) if lines[j].startswith("phase="))
        rows = list(csv.DictReader(io.StringIO("\n".join(lines[i + 1:end])), delimiter="\t"))
        summary = fields(lines[end])
        assert sorted(int(r["request_id"]) for r in rows) == list(range(len(rows)))
        assert summary["failed"] == summary["dropped"] == "0"
        assert len(rows) == int(summary["generated"])
        for outcome, key in (("committed", "completed"), ("rejected_queue_full", "rejected"), ("cancelled_shutdown", "cancelled")):
            assert sum(r["outcome"] == outcome for r in rows) == int(summary[key])
        high = [r for r in rows if r["priority"] == "high"]
        for r in rows:
            assert r["outcome"] != "pending"
            if r["outcome"] == "committed":
                assert int(r["generated_ns"]) <= int(r["admitted_ns"]) <= int(r["first_start_ns"]) <= int(r["finished_ns"])
                assert int(r["attempts"]) == int(r["aborts"]) + 1
        for meta_key, value in (("high_commits", sum(r["outcome"] == "committed" for r in high)), ("high_attempts", sum(int(r["attempts"]) for r in high))):
            if meta_key in meta:
                assert int(meta[meta_key]) == value
        if "checksum" in meta:
            assert next(r for r in rows if r["request_id"] == "0")["q2_checksum"] == meta["checksum"]
        if "real_handlers" in meta:
            assert int(meta["real_handlers"]) >= int(meta.get("async_captures", 0))
            if not signal:
                assert meta["real_handlers"] == "0" and meta.get("async_captures", "0") == "0"
        if "delayed" in meta:
            assert meta["switches"] == meta["low_resumes"] == "1" and meta["deferrals"] == "2"
        records += len(rows)
    assert cases == expected_cases, (path, cases)
    return {"log": path.name, "cases": cases, "records": records, "events": event_count, "schedules": groups}


def main(root):
    result = []
    for suite, count in (("notification_model", 2), ("batched_notifications", 11), ("starvation", 4)):
        names = [f"{suite}-{build}.log" for build in ("debug", "optimized", "ubsan")]
        names += [f"{suite}-repeat-{n}.log" for n in range(1, 21)]
        for name in names:
            row = inspect(root / name, count)
            if suite == "notification_model":
                assert row["events"] == 35 and row["schedules"] == 4
            result.append(row)
    result.append(inspect(root / "tpcc-checkpoint.log", 15))
    result.append(inspect(root / "tpcc-signal-regression.log", 13, signal=True))
    assert "PASS two Silo Q2 scheduled receiver/resumption/teardown cases" in (root / "tpcc-checkpoint.log").read_text()
    assert "PASS 53 actual Silo failed-write rollback/cleanup probes" in (root / "tpcc-signal-regression.log").read_text()
    summary = {key: sum(row[key] for row in result) for key in ("cases", "records", "events", "schedules")}
    (root / "reconciliation.json").write_text(json.dumps({"totals": summary, "logs": result}, indent=2) + "\n")
    print("PASS independent event replay and record reconciliation", json.dumps(summary))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
