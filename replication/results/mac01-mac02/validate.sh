#!/bin/bash
set -euxo pipefail

evidence=/evidence
mkdir -p "$evidence"
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT

apt-get update > "$evidence/apt.log" 2>&1
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential autoconf automake git python3 libnuma-dev libdb++-dev \
  zlib1g-dev libaio-dev libssl-dev >> "$evidence/apt.log" 2>&1

work=$(mktemp -d /work.XXXXXX)
tar --no-same-owner -C "$work" -xf /source.tar
cd "$work/source"
test "$(uname -m)" = x86_64
test "$(git rev-parse HEAD)" = 8395daba617e83e6805268bda793ab0d5359d63b
test "$(git -C masstree rev-parse HEAD)" = "$(git rev-parse HEAD:masstree)"
test "$(git -C masstree rev-parse HEAD)" = 1150824e6a10a0384ba4bfed6b916ce62f96315f
test -z "$(git -C masstree status --porcelain)"
git fsck --no-dangling
git -C masstree fsck --no-dangling
git diff --check
git diff --binary HEAD > "$evidence/build-compatibility.diff"
{
  uname -smr
  cat /etc/os-release
  g++ --version | sed -n '1p'
  ldd --version | sed -n '1p'
  make --version | sed -n '1p'
  python3 --version
  nproc
  taskset -pc "$$"
  sed -n '/^model name/{p;q;}' /proc/cpuinfo
  sed -n '/^MemTotal:/p' /proc/meminfo
  git rev-parse HEAD
  git submodule status
  git status --short
} > "$evidence/environment.log"

make -j1 -C third-party/lz4 library > "$evidence/build.log" 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> "$evidence/build.log" 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test \
  >> "$evidence/build.log" 2>&1

out=out-perf.masstree.contexts
dbtest="$PWD/$out/benchmarks/dbtest"
timeout 90s "./$out/replication/tests/tpcc_mixed_test" 2>&1 | tee "$evidence/tpcc-mixed-test.log"
timeout 90s "./$out/replication/tests/silo_context_test" 2>&1 | tee "$evidence/silo-context-test.log"

c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread -DSILO_WORKER_CONTEXTS \
  benchmarks/request_driver.cc benchmarks/worker_context.cc replication/tests/request_driver_test.cc \
  -o "$work/request_driver_test"
timeout 90s "$work/request_driver_test" 2>&1 | tee "$evidence/request-driver.log"

python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v \
  2>&1 | tee "$evidence/experiment-tests.log"

for build in debug optimized ubsan; do
  case "$build" in
    debug) flags=(-O0 -g) ;;
    optimized) flags=(-O3 -DNDEBUG) ;;
    ubsan) flags=(-O2 -g -fsanitize=undefined -fno-sanitize-recover=all) ;;
  esac
  c++ -std=c++11 "${flags[@]}" -Wall -Wextra -Werror -pedantic -pthread \
    benchmarks/worker_context.cc replication/tests/worker_context_test.cc \
    replication/tests/worker_context_abi.S -o "$work/worker_context_test"
  timeout 180s "$work/worker_context_test" 2>&1 | tee "$evidence/worker-context-$build.log"
done

python3 -B replication/experiments/run.py replication/experiments/configs/queued-cooperative-smoke.json \
  --source "$PWD" --dbtest "$dbtest" --output "$evidence/batches" \
  --build-info 'GCC 11.4; glibc 2.35; MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1; temporary -O1 and GCC pair formatter patch; x86-64 QEMU/Colima Ubuntu 22.04 container'
batches=("$evidence"/batches/*)
test "${#batches[@]}" -eq 1
batch="${batches[0]}"
python3 -B replication/experiments/analyze.py --output "$evidence/analysis" batch "$batch"
python3 -B replication/tests/check_cooperative_batch.py "$batch" | tee "$evidence/reconciliation.log"

timeout 120s "$dbtest" --db-type ndb-proto2 --bench tpcc --scale-factor 1 --num-threads 1 \
  --runtime 1 --queued mac02-default --queued-mixed --queued-policy cooperative --queued-warmup 0 \
  --queued-interval-us 100000 > "$evidence/default-interval.tsv" 2> "$evidence/default-interval.err"
python3 - "$evidence/default-interval.tsv" <<'PY' | tee "$evidence/default-interval-check.log"
import csv
import sys

with open(sys.argv[1]) as stream:
    rows = list(csv.DictReader(stream, delimiter="\t"))
assert all(row["outcome"] == "committed" for row in rows)
q2s = [row for row in rows if row["type"] == "Q2"]
reads = [int(row["cooperative_reads"]) for row in q2s]
assert 0 < min(reads) <= max(reads) < 10_000
assert all(row["cooperative_checks"] == row["cooperative_yields"] == "0" for row in q2s)
print(f"PASS 10,000-read default: {len(q2s)} Q2 commits, "
      f"{min(reads)}–{max(reads)} reads per Q2, zero checks and handoffs")
PY
