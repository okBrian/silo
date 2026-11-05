#!/bin/bash
# Build in a new checkout; SOURCE and all prior build/evidence directories stay intact.
set -euxo pipefail
if [[ $# != 2 ]]; then
  echo "usage: bash validate-local.sh INITIALIZED_SOURCE NEW_EVIDENCE_DIR" >&2
  exit 2
fi
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
source_dir=$(cd "$1" && pwd)
kit=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkdir "$2"
evidence=$(cd "$2" && pwd)
exec > >(tee "$evidence/validation.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT
revision=$(git -C "$source_dir" rev-parse HEAD)
masstree_revision=$(git -C "$source_dir" rev-parse HEAD:masstree)
test "$(git -C "$source_dir/masstree" rev-parse HEAD)" = "$masstree_revision"
test -z "$(git -C "$source_dir/masstree" status --porcelain)"
# Build the recorded commit, never silently omit modifications to engine sources.
git -C "$source_dir" diff --exit-code HEAD -- '*.cc' '*.h' '*.S' Makefile config/config-perf.mk
work=$(mktemp -d)
git clone --no-local --no-checkout "$source_dir" "$work/source"
git -C "$work/source" checkout --detach "$revision"
git -C "$work/source" -c protocol.file.allow=always \
  -c submodule.masstree.url="$source_dir/masstree" submodule update --init --checkout masstree
cd "$work/source"
test "$(git -C masstree rev-parse HEAD)" = "$masstree_revision"
test -z "$(git status --porcelain)"
{
  uname -a
  cat /etc/os-release
  c++ --version
  getconf GNU_LIBC_VERSION
  python3 --version
  lscpu
  taskset -pc "$$"
  git rev-parse HEAD
  git submodule status
} > "$evidence/environment.log"
mkdir "$evidence/tools"
cp "$kit"/{run.py,analyze.py,report.py,validate-local.sh} "$evidence/tools/"
cp "$kit/configs/queued-cooperative-smoke.json" "$evidence/tools/"
cp "$kit/../tests/check_cooperative_batch.py" "$evidence/tools/"
cp "$kit/../tests/test_i05_experiments.py" "$evidence/tools/"
sha256sum "$evidence"/tools/* > "$evidence/tools.sha256"
# Apply only in the disposable build copy; archive the exact resulting diff.
git apply "$source_dir/replication/results/mac01-mac02/build-compatibility.diff"
git diff --binary HEAD > "$evidence/build-compatibility.diff"
make clean > "$evidence/build.log" 2>&1
make -j1 -C third-party/lz4 library >> "$evidence/build.log" 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> "$evidence/build.log" 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test \
  >> "$evidence/build.log" 2>&1
out=out-perf.masstree.contexts
timeout 90s "./$out/replication/tests/tpcc_mixed_test"
timeout 90s "./$out/replication/tests/silo_context_test"
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -pthread -DSILO_WORKER_CONTEXTS \
  benchmarks/request_driver.cc benchmarks/worker_context.cc benchmarks/signal_backend.cc replication/tests/request_driver_test.cc \
  -o "$work/request_driver_test"
timeout 90s "$work/request_driver_test"
python3 -B -m unittest discover -s "$kit/../tests" -p 'test_i05_experiments.py' -v
build_info="$(c++ -dumpfullversion); $(getconf GNU_LIBC_VERSION); MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1; -O1 GCC pair formatter compatibility patch; ${MAC05_ENVIRONMENT:-Linux software smoke}"
python3 -B "$evidence/tools/run.py" "$evidence/tools/queued-cooperative-smoke.json" \
  --source "$PWD" --dbtest "$PWD/$out/benchmarks/dbtest" --output "$evidence/batches" \
  --build-info "$build_info"
batches=("$evidence"/batches/*)
test "${#batches[@]}" -eq 1
batch="${batches[0]}"
python3 -B "$evidence/tools/analyze.py" --output "$evidence/analysis" batch "$batch"
python3 -B "$evidence/tools/report.py" "$evidence/analysis/analysis.json" \
  --output "$evidence/analysis" --label "${MAC05_ENVIRONMENT:-Linux software smoke}; one-second smoke estimates" \
  --require-complete
python3 -B "$evidence/tools/check_cooperative_batch.py" "$batch" | tee "$evidence/reconciliation.log"
# Replay the transferable archive, rather than reusing the live batch directory.
tar -czf "$evidence/batch.tar.gz" -C "$evidence/batches" "$(basename "$batch")"
mkdir "$evidence/replay"
tar -xzf "$evidence/batch.tar.gz" -C "$evidence/replay"
python3 -B "$evidence/tools/analyze.py" --output "$evidence/replay-analysis" \
  batch "$evidence/replay/$(basename "$batch")"
python3 -B "$evidence/tools/report.py" "$evidence/replay-analysis/analysis.json" \
  --output "$evidence/replay-analysis" --label "${MAC05_ENVIRONMENT:-Linux software smoke}; one-second smoke estimates" \
  --require-complete
for table in queued-summary.csv per-type.csv run-health.csv report.md; do
  cmp "$evidence/analysis/$table" "$evidence/replay-analysis/$table"
done
echo 'PASS saved batch regenerates identical analysis CSVs and report'
