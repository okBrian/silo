"""Fixtures and archived evidence for the experiment runner and analysis."""

import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPLICATION = Path(__file__).resolve().parents[1]
EXPERIMENTS = REPLICATION / "experiments"
FIXTURES = Path(__file__).parent / "fixtures" / "i05"
RESULTS = REPLICATION / "results"
sys.path.insert(0, str(EXPERIMENTS))
import analyze
import run


class AnalysisTests(unittest.TestCase):
    def test_stock_columns_and_units(self):
        stock = analyze.parse_stock((FIXTURES / "stock.stdout").read_text(), "runtime: 1.003 sec\n")
        self.assertEqual(list(stock.values()), [100, 90, 0.0125, 0.003, 4.5, 12.5, 1.003])
        self.assertIsNone(analyze.parse_stock("0 0 -nan 0 0")["avg_attempt_latency_us"])
        with self.assertRaises(ValueError):
            analyze.parse_stock("1 2 3 4")
        with self.assertRaises(ValueError):
            analyze.parse_stock("1 2 3 4 5\n6 7 8 9 10")

    def test_nearest_rank_and_tail_threshold(self):
        values = list(range(1000, 11000, 1000))
        self.assertEqual(analyze.distribution(reversed(values)), {
            "n": 10, "p50_us": 5, "p90_us": 9, "p99_us": 10, "p99_9_us": 10, "p99_9_low_sample": True,
        })
        self.assertTrue(analyze.distribution([1] * 999)["p99_9_low_sample"])
        self.assertFalse(analyze.distribution([1] * 1000)["p99_9_low_sample"])
        self.assertEqual(analyze.distribution([1000] * 999 + [2000])["p99_9_us"], 1)
        self.assertIsNone(analyze.distribution([])["p99_9_us"])

    def test_queued_cohort(self):
        raw, stderr = ((FIXTURES / f"queued.{ext}").read_text() for ext in ("tsv", "err"))
        queued = analyze.parse_queued(raw, stderr, "fixture", {"measurement": 1})
        self.assertEqual(queued["issues"], [])
        cohort, new_order, payment = queued["groups"]
        self.assertEqual([cohort[k] for k in ("generated", "committed", "rejected", "cancelled",
                                             "commits_in_window", "drain_commits", "drain_completions",
                                             "window_throughput_txn_s", "attempts", "aborts")],
                         [5, 2, 1, 1, 1, 1, 2, 1, 4, 1])
        for key, median, tail in (("queue", 3, 7), ("end_to_end", 8, 18),
                                  ("global_queue", 1, 4), ("local_queue", 2, 3), ("scheduled", 9, 19)):
            stats = cohort["latency"][key]
            self.assertEqual((stats["n"], stats["p50_us"], stats["p99_9_us"]), (2, median, tail))
            self.assertTrue(stats["p99_9_low_sample"])
        self.assertEqual(new_order["drain_commits"], 1)
        self.assertEqual(payment["latency"]["queue"]["n"], 0)
        self.assertIsNone(payment["latency"]["queue"]["p50_us"])
        self.assertIsNone(analyze.parse_queued(raw, "")["groups"][0]["drain_commits"])
        truncated = "\n".join(raw.splitlines()[:-1])
        self.assertEqual(analyze.parse_queued(truncated, stderr)["groups"], [])
        with self.assertRaisesRegex(ValueError, "duplicate request ID"):
            analyze.parse_queued(raw + raw.splitlines()[-1], stderr)
        with self.assertRaisesRegex(ValueError, "lifecycle timestamps"):
            analyze.parse_queued(raw.replace("\t5000\t10000", "\t500\t10000"), stderr)

    def test_pace_archive_matches_published_values(self):
        result = analyze.load_pace(RESULTS / "pace-runs" / "6791465")
        self.assertEqual(result["batch"]["job_exit_status"], 134)
        published = RESULTS / "i04-pace-baseline"
        with (published / "stock-tpcc.csv").open(newline="") as file:
            rows = list(csv.DictReader(file))
        self.assertEqual(len(rows), len(result["runs"]))
        for row, measured in zip(rows, result["runs"]):
            self.assertEqual(measured["status"], "complete")
            self.assertIsNone(measured["exit_status"])
            for key in ("workers", "repeat", "warehouses"):
                self.assertEqual(measured[key], int(row[key]))
            for key in analyze.STOCK_FIELDS:
                self.assertEqual(measured["stock"][key], float(row[key]))
        summary = (published / "SUMMARY.md").read_text()
        for line in analyze.stock_markdown(analyze.stock_table(result["runs"])).splitlines():
            if line.startswith("| stock-tpcc-2wh"):
                cells = line.split(" | ")
                self.assertIn("| " + " | ".join([cells[3], cells[5], *cells[7:]]), summary)

    def test_i06_archives(self):
        paths = sorted((RESULTS / "i06-request-driver").glob("queued-w*.tsv"))
        runs = analyze.load_queued(paths, 1)["runs"]
        self.assertEqual([record["status"] for record in runs], ["phase-verified"] * 2)
        measured = [row for row in analyze.queued_table(runs)
                    if row["phase"] == "measurement" and row["type"] == "All"]
        self.assertEqual([row["commits_in_window"] for row in measured], [36, 72])
        self.assertEqual([row["drain_commits"] for row in measured], [0, 0])
        self.assertEqual([row["queue_n"] for row in measured], [36, 72])
        self.assertEqual([row["end_to_end_n"] for row in measured], [36, 72])
        self.assertTrue(all(row["end_to_end_p99_9_low_sample"] for row in measured))
        self.assertEqual(runs[0]["queued"]["phases"]["warmup"]["commits_in_window"], 36)


class RunnerTests(unittest.TestCase):
    def test_matrix_continues_after_failure_and_retains_outputs(self):
        config = json.loads((EXPERIMENTS / "configs" / "stock-tpcc-2wh.json").read_text())
        config.update(repeats=2, timeout_s=10)
        config["phases_s"]["measurement"] = 1
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = root / "fixture dbtest"
            binary.write_text("#!/bin/sh\n"
                              "echo fixture-stderr >&2\n"
                              'case "$*" in *"--num-threads 2 "*) exit 7;; esac\n'
                              "echo '100 90 0.0125 0.003 4.5'\n")
            binary.chmod(0o755)
            source = REPLICATION.parent
            args = [str(binary), "--source", str(source), "--build-info", "shell fixture",
                    "--output", str(root / "results")]
            settings = root / "config.json"
            settings.write_text(json.dumps(config))
            command = [sys.executable, "-B", str(EXPERIMENTS / "run.py"), str(settings), "--dbtest", *args]
            for _ in range(2):
                completed = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(completed.returncode, 1, completed.stderr)
            batches = sorted((root / "results").iterdir())
            self.assertEqual(len(batches), 2)
            seen_ids = set()
            for directory in batches:
                result = analyze.load_batch(directory)
                self.assertEqual(result["batch"]["config"], config)
                self.assertEqual(len(result["batch"]["binary_sha256"]), 64)
                self.assertEqual(len(result["batch"]["revisions"]["silo"]["head"]), 40)
                self.assertTrue((directory / "silo.diff").is_file())
                runs = result["runs"]
                self.assertEqual([row["workers"] for row in runs], [1, 2, 2, 1])
                self.assertEqual([row["exit_status"] for row in runs], [0, 7, 7, 0])
                self.assertEqual([row["status"] for row in runs], ["complete", "failed", "failed", "complete"])
                self.assertEqual([row["stock"] for row in runs if row["status"] == "failed"], [None, None])
                self.assertEqual(runs[0]["command"][0], str(binary.resolve()))
                self.assertEqual(runs[0]["seeds"]["tpcc_serial_loaders"]["warehouse"], 9324)
                self.assertEqual(Path(runs[1]["stderr"]).read_text(), "fixture-stderr\n")
                self.assertTrue(all("cpu_placement" in row and "wall_elapsed_s" in row for row in runs))
                seen_ids.update(row["run_id"] for row in runs)
                analyze.write_results(result, root / "analysis")
                with (root / "analysis" / "stock-runs.csv").open(newline="") as file:
                    self.assertEqual(list(csv.DictReader(file))[1]["throughput_ops_s"], "")
            self.assertEqual(len(seen_ids), 8)

    def test_queued_configuration_passes_seed_and_phase_options(self):
        config = json.loads((EXPERIMENTS / "configs" / "queued-tpcc-smoke.json").read_text())
        run.validate(config, [1, 2, 3])
        variant = dict(config["variants"][1], cpus=[2, 3])
        planned = run.plan_run(config, variant, 2, 2, Path("/tmp/batch"), Path("/tmp/dbtest"), [1, 2, 3])
        self.assertEqual(planned["command"][:3], ["taskset", "--cpu-list", "2,3"])
        self.assertEqual(planned["queue"]["batch-size"], 8)
        self.assertEqual(planned["queue"]["seed"], 2)
        self.assertEqual(planned["seeds"]["queued_warmup"], 2 ^ 0x9E3779B97F4A7C15)
        self.assertEqual(planned["phases_s"], {"warmup": 1, "measurement": 1, "drain_limit": 30})
        self.assertNotIn("--queued-mixed", planned["command"])

    def test_mixed_configuration_records_q2_dataset_and_profile(self):
        config = json.loads((EXPERIMENTS / "configs" / "queued-mixed-smoke.json").read_text())
        run.validate(config, None)
        planned = run.plan_run(config, config["variants"][1], 1, 2,
                               Path("/tmp/batch"), Path("/tmp/dbtest"), None)
        self.assertIn("--queued-mixed", planned["command"])
        self.assertEqual(planned["q2_variant"], "q2_min_stock_v1")
        self.assertEqual(planned["workload"]["q2_supplier_scan_count"], 100)
        self.assertEqual(planned["seeds"]["q2_supplier_loader"], 51271928)
        config["queue"]["q2_variant"] = "SyntheticLow"
        with self.assertRaises(ValueError):
            run.validate(config, None)

    def test_timeout_retains_run(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            command = [sys.executable, "-c", "import time; print('started', flush=True); time.sleep(30)"]
            self.assertFalse(run.execute(directory, {"command": command, "timeout_s": 0.5}))
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(manifest["state"], "timed_out")
            self.assertLess(manifest["exit_status"], 0)
            self.assertEqual((directory / "stdout.log").read_text(), "started\n")


if __name__ == "__main__":
    unittest.main()
