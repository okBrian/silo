#!/usr/bin/env python3
"""Freeze historical evidence and package report/source without Git history or binaries."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
FROZEN = "7319ba60394b6d68a4d8f6382177c5e5758c6b28"
DEPENDENCY = "1150824e6a10a0384ba4bfed6b916ce62f96315f"
EVIDENCE = ROOT / "replication/results"
OUT = EVIDENCE / "mac11-midpoint"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--masstree-archive", type=Path, required=True,
                        help="GitHub source tar.gz for pinned Masstree revision 1150824")
    parser.add_argument("--output", type=Path, default=ROOT / "output/midpoint/local-midpoint-package.zip")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    records = []
    for directory in sorted(EVIDENCE.glob("mac*")):
        if directory.name == "mac11-midpoint" or not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                data = path.read_bytes()
                records.append(dict(path=str(path.relative_to(ROOT)), bytes=len(data), sha256=digest(data)))
    metadata = dict(date="2026-11-05", branch="erin/local-replication-evidence",
                    frozen_source=FROZEN, masstree=DEPENDENCY,
                    environment="Historical Apple M5 Pro / Colima-QEMU x86-64 Linux; per-run environment retained",
                    new_execution="MAC-11 analysis replay, report compilation/render and package checks only",
                    submission_deadline=None, final_deadline=None, submitter=None,
                    original_proposal_diff_verified=False, files=records)
    (ROOT / "replication/midpoint/inventory.json").write_text(json.dumps(metadata, indent=2) + "\n")
    continuity = []
    for manifest, selected in (("mac10-notification-model/source.sha256", None),
                               ("mac09-signal/final/source.sha256", {
                                   "benchmarks/request_driver.cc", "benchmarks/request_driver.h",
                                   "benchmarks/worker_context.cc", "benchmarks/worker_context.h",
                                   "benchmarks/signal_backend.cc", "benchmarks/signal_backend.h"})):
        for line in (EVIDENCE / manifest).read_text().splitlines():
            expected, name = line.split(None, 1)
            name = name.strip()
            if selected is not None and name not in selected:
                continue
            actual = digest((ROOT / name).read_bytes())
            assert actual == expected, (manifest, name)
            continuity.append(dict(manifest=manifest, path=name, sha256=actual))
    (OUT / "source-continuity.json").write_text(json.dumps(continuity, indent=2) + "\n")
    # git archive captures the exact base; a narrow overlay contains only MAC-11 outputs.
    raw = subprocess.check_output(["git", "archive", "--format=tar", FROZEN], cwd=ROOT)
    files = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for member in archive.getmembers():
            if member.isfile():
                files["silo/" + member.name] = archive.extractfile(member).read()
            else:
                assert member.isdir(), member.name
    dep = args.masstree_archive.read_bytes()
    with tarfile.open(fileobj=io.BytesIO(dep), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts
            assert parts[0] == "masstree-beta-" + DEPENDENCY
            if member.isfile():
                files["silo/masstree/" + str(Path(*parts[1:]))] = archive.extractfile(member).read()
            else:
                assert member.isdir(), member.name
    for path in (ROOT / "replication/midpoint", OUT, ROOT / "replication/LOCAL_REPLICATION_PLAN.md",
                 ROOT / "replication/IMPLEMENTATION_STATUS.md", ROOT / "output/pdf/project_milestone.pdf"):
        paths = path.rglob("*") if path.is_dir() else [path]
        for item in paths:
            if item.is_file() and "__pycache__" not in item.parts and item.name != "package.log":
                files["silo/" + str(item.relative_to(ROOT))] = item.read_bytes()
    freeze = dict(frozen_source=FROZEN, masstree=DEPENDENCY, dependency_archive_sha256=digest(dep),
                  overlay="MAC-11 report, generated data, evidence inventory, verification and status documents",
                  history_included=False, executables_included=False)
    files["SOURCE-FREEZE.json"] = (json.dumps(freeze, indent=2) + "\n").encode()
    files["README.txt"] = ("Local midpoint package - Group 20\n"
                           "Report: silo/output/pdf/project_milestone.pdf\n"
                           "Editable source/reproduction: silo/replication/midpoint/README.md\n"
                           "Evidence/remaining requirements: silo/replication/midpoint/EVIDENCE.md\n"
                           "Deadlines, submitter, original-proposal diff and course sign-off remain pending.\n").encode()
    hashes = {name: digest(data) for name, data in sorted(files.items())}
    files["PACKAGE-SHA256.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive:
        assert archive.testzip() is None
        for name, expected in hashes.items():
            assert digest(archive.read(name)) == expected, name
    print(f"PASS package CRC and {len(hashes)} SHA-256 entries; {len(records)} frozen evidence files; "
          f"{len(continuity)} tested-source hash matches")
    print(args.output)
    print("ZIP SHA-256", digest(args.output.read_bytes()))


if __name__ == "__main__":
    main()
