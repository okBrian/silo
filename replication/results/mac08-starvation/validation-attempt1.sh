#!/bin/bash
# Run only in a disposable, initialized x86-64 Linux source copy.
set -euxo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
test $# = 1
mkdir -p "$1"
evidence=$(cd "$1" && pwd)
exec > >(tee "$evidence/validation.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT
test "$(git rev-parse --show-toplevel)" = "$PWD"
git rev-parse HEAD > "$evidence/source-base"
{ uname -a; cat /etc/os-release; c++ --version; getconf GNU_LIBC_VERSION; lscpu; taskset -pc "$$"; } > "$evidence/environment.log"
test "$(git -C masstree rev-parse HEAD)" = 1150824e6a10a0384ba4bfed6b916ce62f96315f
sha256sum benchmarks/{request_driver.cc,request_driver.h,starvation_budget.h,dbtest.cc} replication/experiments/run.py \
  replication/tests/{starvation_cases.h,starvation_test.cc,tpcc_mixed_test.cc,validate_starvation.sh,test_i05_experiments.py} \
  replication/experiments/configs/queued-starvation-*.json > "$evidence/source.sha256"
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -pthread)
for build in debug optimized ubsan; do
  case "$build" in
    debug) opt=(-O0 -g) ;;
    optimized) opt=(-O3) ;;
    ubsan) opt=(-O2 -fsanitize=undefined -fno-sanitize-recover=all) ;;
  esac
  c++ "${flags[@]}" "${opt[@]}" -DSILO_WORKER_CONTEXTS benchmarks/request_driver.cc benchmarks/worker_context.cc \
    replication/tests/starvation_test.cc -o "$evidence/starvation-$build"
  timeout 60s "$evidence/starvation-$build" > "$evidence/starvation-$build.log" 2>&1
done
for repetition in {1..20}; do
  timeout 60s "$evidence/starvation-optimized" > "$evidence/repeat-$repetition.log" 2>&1
done
for build in wait cooperative; do
  context=()
  if [[ "$build" == cooperative ]]; then context=(-DSILO_WORKER_CONTEXTS benchmarks/worker_context.cc); fi
  c++ "${flags[@]}" -O2 "${context[@]}" benchmarks/request_driver.cc replication/tests/request_driver_test.cc -o "$evidence/driver-$build"
  timeout 60s "$evidence/driver-$build" > "$evidence/driver-$build.log" 2>&1
done
python3 -B -m unittest discover -s replication/tests -p test_i05_experiments.py -v > "$evidence/experiments.log" 2>&1
git apply replication/results/mac01-mac02/build-compatibility.diff
make -j1 -C third-party/lz4 library > "$evidence/build.log" 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> "$evidence/build.log" 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test >> "$evidence/build.log" 2>&1
out=out-perf.masstree.contexts
timeout 120s "./$out/replication/tests/tpcc_mixed_test" > "$evidence/tpcc-mixed.log" 2>&1
timeout 120s "./$out/replication/tests/silo_context_test" > "$evidence/silo-context.log" 2>&1
for threshold in 25 100; do
  python3 -B replication/experiments/run.py "replication/experiments/configs/queued-starvation-$threshold.json" \
    --source "$PWD" --dbtest "$out/benchmarks/dbtest" --output "$evidence/pressure-$threshold" \
    --build-info "GCC 11.4; WORKER_CONTEXTS=1; MYSQL=0; USE_MALLOC_MODE=0; archived MAC-01 -O1 compatibility patch"
  python3 -B replication/experiments/analyze.py batch "$evidence/pressure-$threshold" --output "$evidence/analysis-$threshold"
  python3 -B replication/experiments/report.py "$evidence/analysis-$threshold/analysis.json" --output "$evidence/report-$threshold" \
    --label "MAC-08 emulated x86-64 Linux functional pressure test; monotonic ns budget" --require-complete
done
sha256sum "$out"/benchmarks/dbtest "$out"/replication/tests/{tpcc_mixed_test,silo_context_test} "$evidence"/starvation-{debug,optimized,ubsan} > "$evidence/artifacts.sha256"
echo 'PASS MAC-08 controlled checkpoint integration and real Q2 pressure; signal repeat pending MAC-09'
