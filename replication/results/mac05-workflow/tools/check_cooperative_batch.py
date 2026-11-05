#!/usr/bin/env python3
"""Validate interval counts, dispatch order and input matching in an I11 smoke batch."""

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def check_run(run_dir, manifest):
    with (run_dir / "stdout.log").open(newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert manifest["exit_status"] == 0
    assert len({row["request_id"] for row in rows}) == len(rows)
    assert all(row["outcome"] == "committed" for row in rows)
    interval = manifest["yield_interval"]
    high_inputs = []
    low_inputs = {}
    for phase in ("warmup", "measurement"):
        phase_rows = [row for row in rows if row["phase"] == phase]
        assert phase_rows
        highs = [row for row in phase_rows if row["priority"] == "high"]
        lows = [row for row in phase_rows if row["type"] == "Q2"]
        checks = yields = nested = 0
        for row in phase_rows:
            reads, tested, handed_off = (int(row[f"cooperative_{key}"])
                                         for key in ("reads", "checks", "yields"))
            if not interval or row["type"] != "Q2":
                assert reads == tested == handed_off == 0
            else:
                attempts = int(row["attempts"])
                assert reads > 0 and 0 <= handed_off <= tested
                # Each attempt can discard a partial interval when it ends.
                assert reads // interval - (attempts - 1) <= tested <= reads // interval
            checks += tested
            yields += handed_off
        for worker in range(manifest["workers"]):
            local_highs = [row for row in highs if int(row["worker"]) == worker]
            local_lows = [row for row in lows if int(row["worker"]) == worker]
            for group in (local_highs, local_lows):
                group.sort(key=lambda row: int(row["request_id"]))
                assert all(int(left["finished_ns"]) <= int(right["first_start_ns"])
                           for left, right in zip(group, group[1:]))
            low_inputs[phase, worker] = [row["input_seed"] for row in local_lows]
            for low in local_lows:
                start, end = int(low["first_start_ns"]), int(low["finished_ns"])
                inside = 0
                for high in local_highs:
                    first, last = int(high["first_start_ns"]), int(high["finished_ns"])
                    if first < end and last > start:
                        assert start <= first <= last <= end
                        inside += 1
                handoffs = int(low["cooperative_yields"])
                assert (inside > 0) == (handoffs > 0) and inside >= handoffs
                nested += inside
        first_deadline = min(int(row["scheduled_ns"]) for row in highs)
        high_inputs += [
            (phase, int(row["scheduled_ns"]) - first_deadline, row["batch_id"],
             row["type"], row["worker"], row["home_warehouse"], row["input_seed"])
            for row in sorted(highs, key=lambda row: int(row["request_id"]))
        ]
        if phase == "measurement" and interval:
            assert nested > 0
        print(f"PASS repeat={manifest['repeat']} workers={manifest['workers']} "
              f"policy={manifest['policy']} phase={phase} high={len(highs)} Q2={len(lows)} "
              f"checks={checks} yields={yields} high_during_q2={nested}")
    return high_inputs, low_inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    batch = parser.parse_args().batch
    metadata = json.loads((batch / "batch.json").read_text())
    pairs = defaultdict(dict)
    for run_id in metadata["run_ids"]:
        run_dir = batch / run_id
        manifest = json.loads((run_dir / "manifest.json").read_text())
        pairs[manifest["repeat"], manifest["workers"]][manifest["policy"]] = (
            manifest, check_run(run_dir, manifest))
    for (repeat, workers), pair in sorted(pairs.items()):
        wait, (wait_high, wait_low) = pair["wait"]
        cooperative, (coop_high, coop_low) = pair["cooperative"]
        for key in ("seeds", "queue", "phases_s", "q2_variant", "workload", "cpu_placement"):
            assert wait[key] == cooperative[key]
        assert wait_high == coop_high
        for key in wait_low:
            shared = min(len(wait_low[key]), len(coop_low[key]))
            assert shared and wait_low[key][:shared] == coop_low[key][:shared]
        print(f"PASS repeat={repeat} workers={workers} matching high arrivals and Q2 input prefixes")


if __name__ == "__main__":
    main()
