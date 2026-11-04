#!/usr/bin/env python3
"""Run a matrix against a built dbtest and retain an audit trail per invocation."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import socket
import subprocess
import tempfile
import time


LOADER_SEEDS = {
    "warehouse": 9324, "item": 235443, "stock": 89785943,
    "district": 129856349, "customer": 923587856425, "order": 2343352,
}
PHASES = {"warmup", "measurement", "drain_limit"}
QUEUE = {
    "first_seed", "interval_us", "batch_per_worker",
    "global_capacity_per_worker", "worker_capacity",
}
Q2_VARIANT = "q2_min_stock_v1"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, minimum=1, maximum=None):
    return type(value) is int and value >= minimum and (maximum is None or value <= maximum)


def validate(config, allowed):
    require(isinstance(config, dict), "config must be a JSON object")
    mode = config.get("format")
    require(mode in ("stock", "queued"), "format must be stock or queued")
    fields = {"name", "format", "variants", "repeats", "phases_s", "timeout_s"}
    require(config.keys() == fields | ({"queue"} if mode == "queued" else set()),
            "unexpected or missing config keys")
    require(isinstance(config["name"], str) and re.fullmatch(r"[A-Za-z0-9_-]+", config["name"]),
            "name must contain only ASCII letters, digits, hyphens, and underscores")
    require(integer(config["repeats"]) and integer(config["timeout_s"]),
            "repeats and timeout_s must be positive integers")
    phases = config["phases_s"]
    require(isinstance(phases, dict) and phases.keys() == PHASES and
            all(integer(value, 0, 86400) for value in phases.values()) and phases["measurement"] > 0,
            "phases_s requires warmup, positive measurement, and drain_limit in whole seconds")
    require(config["timeout_s"] > sum(phases.values()) + phases["drain_limit"],
            "timeout_s must allow for the phases, both possible drains, and setup")
    if mode == "stock":
        require(phases["warmup"] == phases["drain_limit"] == 0,
                "stock dbtest has no separate warm-up or drain")
    else:
        queue = config["queue"]
        require(isinstance(queue, dict) and QUEUE <= queue.keys() <= QUEUE | {"q2_variant"} and
                ("q2_variant" not in queue or queue["q2_variant"] == Q2_VARIANT) and
                all(integer(queue[key], 1, 2**32 - 1) for key in QUEUE - {"first_seed"}) and
                integer(queue["first_seed"], 0, 2**64 - config["repeats"]),
                "queue requires positive integer settings and uint64 seeds across all repeats")
    require(isinstance(config["variants"], list) and config["variants"], "variants must be nonempty")
    for variant in config["variants"]:
        require(isinstance(variant, dict) and {"workers", "warehouses"} <= variant.keys()
                <= {"workers", "warehouses", "cpus"}, "invalid variant keys")
        workers, warehouses = variant["workers"], variant["warehouses"]
        require(integer(workers) and integer(warehouses), "workers and warehouses must be positive integers")
        if mode == "queued":
            require(workers == warehouses, "I06 queued mode requires one warehouse per worker")
            batch = queue["batch_per_worker"] * workers
            require(max(batch, queue["global_capacity_per_worker"] * workers) < 2**32,
                    "derived queue sizes must fit in uint32")
            interval_ns = queue["interval_us"] * 1000
            batches = [(phases[phase] * 10**9 - 1) // interval_ns if phases[phase] else 0
                       for phase in ("warmup", "measurement")]
            require(batches[1] > 0 and sum(batches) * batch <= 2**31,
                    "queued run must produce a measured batch and fit within the high-priority date range")
        if "cpus" in variant:
            cpus = variant["cpus"]
            require(isinstance(cpus, list) and cpus and all(integer(cpu, 0) for cpu in cpus)
                    and len(set(cpus)) == len(cpus), "cpus must be a nonempty list of distinct CPU IDs")
            require(allowed is not None and set(cpus) <= set(allowed),
                    "explicit cpus require Linux and must belong to the inherited allocation")


def write_json(path, data):
    # A killed runner leaves the last complete manifest readable.
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as data:
        for chunk in iter(lambda: data.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(source, *args):
    return subprocess.check_output(["git", "-C", str(source), *args])


def provenance(source, binary, batch, build_info):
    repositories = {"silo": source}
    masstree = source / "masstree"
    if (masstree / ".git").exists():
        repositories["masstree"] = masstree
    revisions = {"masstree": None}
    for name, checkout in repositories.items():
        diff = git(checkout, "diff", "--binary", "HEAD", "--")
        (batch / f"{name}.diff").write_bytes(diff)
        untracked = git(checkout, "ls-files", "--others", "--exclude-standard", "-z")
        paths = untracked.decode(errors="replace").split("\0")
        revisions[name] = {
            "head": git(checkout, "rev-parse", "HEAD").decode().strip(),
            "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(),
            "untracked_paths": [path for path in paths if path and batch not in (checkout / path).parents],
        }
    return {"revisions": revisions, "binary_sha256": sha256(binary), "build_info": build_info,
            "tool_sha256": sha256(Path(__file__).resolve())}


def plan_run(config, variant, repeat, variant_index, batch, binary, allowed):
    phases = config["phases_s"]
    workers, warehouses = variant["workers"], variant["warehouses"]
    run_id = f"{batch.name}-r{repeat}-v{variant_index}"
    benchmark = [
        str(binary), "--db-type", "ndb-proto2", "--bench", "tpcc",
        "--scale-factor", str(warehouses), "--num-threads", str(workers),
        "--runtime", str(phases["measurement"]), "--verbose",
    ]
    seeds = {"tpcc_serial_loaders": LOADER_SEEDS, "tpcc_worker_seed_generator": 23984543}
    queue = None
    q2_variant = None
    if config["format"] == "queued":
        settings = config["queue"]
        seed = settings["first_seed"] + repeat - 1
        seeds.update(queued_measurement=seed, queued_warmup=(
            seed ^ 0x9E3779B97F4A7C15 if phases["warmup"] else None))
        queue = {
            "interval-us": settings["interval_us"],
            "batch-size": settings["batch_per_worker"] * workers,
            "global-capacity": settings["global_capacity_per_worker"] * workers,
            "worker-capacity": settings["worker_capacity"],
            "warmup": phases["warmup"], "drain": phases["drain_limit"], "seed": seed,
        }
        benchmark += ["--queued", run_id]
        q2_variant = settings.get("q2_variant")
        if q2_variant:
            benchmark.append("--queued-mixed")
            seeds["q2_supplier_loader"] = 51271928
        for setting, value in queue.items():
            benchmark += [f"--queued-{setting}", str(value)]
    cpus = variant.get("cpus")
    command = (["taskset", "--cpu-list", ",".join(map(str, cpus))] if cpus else []) + benchmark
    workload = "TPC-C New-Order/Payment (I06)" if queue else "stock TPC-C mix"
    if q2_variant:
        workload = {
            "high": {"NewOrder": 0.5, "Payment": 0.5},
            "low": {"Q2": 1.0, "pending_capacity_per_worker": 1},
            "rows": {"regions": 5, "nations": 62, "suppliers": 10000, "items": 100000,
                     "stock_per_warehouse": 100000},
            "q2_supplier_scan_count": 100, "q2_region_selection": "uniform",
            "remote_payment_probability": 0.15 if warehouses > 1 else 0,
            "remote_new_order_item_probability": 0.01 if warehouses > 1 else 0,
            "new_order_item_distribution": "Silo NURand",
        }
    return {
        "contract_version": 1, "run_id": run_id, "config": config["name"],
        "format": config["format"], "repeat": repeat,
        "variant": variant_index, "workers": workers, "warehouses": warehouses, "seeds": seeds,
        "queue": queue, "phases_s": phases, "timeout_s": config["timeout_s"], "command": command,
        "cpu_placement": {"inherited_allowed": allowed, "process_mask": cpus or allowed,
                          "worker_cpu_ids": None, "sender_cpu_id": None},
        "policy": "i09-serial" if q2_variant else "i06-queued" if queue else "stock",
        "q2_variant": q2_variant, "logging": "disabled", "workload": workload,
        "yield_interval": None, "starvation_threshold": None,
        "retry_limit": 100 if queue else None, "batch_metadata": "../batch.json",
        "state": "planned", "exit_status": None,
    }


def execute(run_dir, manifest):
    metadata_path = run_dir / "manifest.json"
    manifest.update(state="running", exit_status=None, started_at_utc=utc_now())
    write_json(metadata_path, manifest)
    start = time.monotonic()
    try:
        with (run_dir / "stdout.log").open("wb") as stdout, (run_dir / "stderr.log").open("wb") as stderr:
            with subprocess.Popen(manifest["command"], cwd=run_dir, stdout=stdout, stderr=stderr) as process:
                try:
                    manifest.update(state="finished", exit_status=process.wait(timeout=manifest["timeout_s"]))
                except subprocess.TimeoutExpired:
                    process.kill()
                    manifest.update(state="timed_out", exit_status=process.wait(),
                                    error="dbtest exceeded its wall-clock timeout")
                except KeyboardInterrupt:
                    process.kill()
                    manifest.update(state="interrupted", exit_status=process.wait())
                    raise
    except OSError as error:
        manifest.update(state="launch_failed", error=str(error))
    finally:
        manifest.update(finished_at_utc=utc_now(), wall_elapsed_s=time.monotonic() - start)
        write_json(metadata_path, manifest)
    return manifest["state"] == "finished" and manifest["exit_status"] == 0


def run(config, source, binary, output, build_info):
    allowed = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    validate(config, allowed)
    output.mkdir(parents=True, exist_ok=True)
    batch = Path(tempfile.mkdtemp(prefix=f"{config['name']}-", dir=output))
    print(f"Experiment directory: {batch}", flush=True)
    metadata = {
        "contract_version": 1, "batch_id": batch.name, "config": config, "created_at_utc": utc_now(),
        "hostname": socket.gethostname(), "platform": platform.platform(),
        "slurm_job_id": os.environ.get("SLURM_JOB_ID"),
        "source_checkout": str(source), **provenance(source, binary, batch, build_info),
    }
    runs = []
    variants = list(enumerate(config["variants"], 1))
    for repeat in range(1, config["repeats"] + 1):
        for index, variant in (variants if repeat % 2 else reversed(variants)):
            manifest = plan_run(config, variant, repeat, index, batch, binary, allowed)
            run_dir = batch / manifest["run_id"]
            run_dir.mkdir()
            write_json(run_dir / "manifest.json", manifest)
            runs.append((run_dir, manifest))
    metadata["run_ids"] = [path.name for path, _ in runs]
    write_json(batch / "batch.json", metadata)
    failures = 0
    for run_dir, manifest in runs:
        print(f"Running {run_dir.name}", flush=True)
        failures += not execute(run_dir, manifest)
    return int(failures > 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--source", type=Path, required=True, help="Git checkout used to build dbtest")
    parser.add_argument("--dbtest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-info", required=True, help="compiler and build flags used for dbtest")
    args = parser.parse_args()
    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        return run(config, args.source.resolve(), args.dbtest.resolve(), args.output.resolve(),
                   args.build_info)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
