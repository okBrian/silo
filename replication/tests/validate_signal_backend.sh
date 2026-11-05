#!/bin/bash
# Run in a disposable initialized x86-64 Linux/glibc source copy.
set -euxo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
test $# = 1
mkdir -p "$1"
evidence=$(cd "$1" && pwd)
exec > >(tee "$evidence/validation.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT
git rev-parse HEAD > "$evidence/source-base"
git diff --binary HEAD > "$evidence/source-tracked.diff"
test "$(git -C masstree rev-parse HEAD)" = 1150824e6a10a0384ba4bfed6b916ce62f96315f
{ uname -a; cat /etc/os-release; c++ --version; getconf GNU_LIBC_VERSION; lscpu; taskset -pc "$$"; } > "$evidence/environment.log"
sha256sum Makefile preemption.h benchmarks/{request_driver.*,worker_context.*,signal_backend.*,bench.cc,dbtest.cc,tpcc_q2.h} \
  replication/tests/{signal_backend_test.cc,batched_notification_cases.h,batched_notifications_test.cc,starvation_cases.h,starvation_test.cc,tpcc_mixed_test.cc,validate_signal_backend.sh,test_i05_experiments.py} \
  replication/experiments/run.py replication/experiments/configs/queued-signal-*.json > "$evidence/source.sha256"
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -pthread)
runtime=(benchmarks/worker_context.cc benchmarks/signal_backend.cc)
for build in debug optimized ubsan; do
  case "$build" in
    debug) opt=(-O0 -g) ;;
    optimized) opt=(-O3) ;;
    ubsan) opt=(-O2 -fsanitize=undefined -fno-sanitize-recover=all) ;;
  esac
  c++ "${flags[@]}" "${opt[@]}" "${runtime[@]}" replication/tests/signal_backend_test.cc -o "$evidence/signal-$build"
  timeout -k 5s 60s "$evidence/signal-$build" > "$evidence/signal-$build.log" 2>&1
  for suite in batched_notifications starvation; do
    c++ "${flags[@]}" "${opt[@]}" -DSILO_WORKER_CONTEXTS benchmarks/request_driver.cc "${runtime[@]}" \
      "replication/tests/${suite}_test.cc" -o "$evidence/$suite-$build"
    timeout -k 5s 60s "$evidence/$suite-$build" signal > "$evidence/$suite-$build.log" 2>&1
  done
done
for repetition in {1..20}; do
  for suite in signal batched_notifications starvation; do
    timeout -k 5s 60s "$evidence/$suite-optimized" signal > "$evidence/$suite-repeat-$repetition.log" 2>&1
  done
done
# Preserve the original checkpoint schedules as regressions on this source.
for suite in batched_notifications starvation; do
  timeout -k 5s 60s "$evidence/$suite-optimized" > "$evidence/$suite-checkpoint.log" 2>&1
done
for build in wait cooperative; do
  context=()
  if [[ "$build" == cooperative ]]; then context=(-DSILO_WORKER_CONTEXTS "${runtime[@]}"); fi
  c++ "${flags[@]}" -O2 "${context[@]}" benchmarks/request_driver.cc replication/tests/request_driver_test.cc -o "$evidence/driver-$build"
  timeout -k 5s 60s "$evidence/driver-$build" > "$evidence/driver-$build.log" 2>&1
done
python3 -B -m unittest discover -s replication/tests -p test_i05_experiments.py -v > "$evidence/experiments.log" 2>&1
objdump -d -C "$evidence/signal-optimized" | gzip > "$evidence/signal-disassembly.txt.gz"
git apply replication/results/mac01-mac02/build-compatibility.diff
make -j1 -C third-party/lz4 library > "$evidence/build.log" 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> "$evidence/build.log" 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test >> "$evidence/build.log" 2>&1
out=out-perf.masstree.contexts
timeout -k 5s 120s "./$out/replication/tests/tpcc_mixed_test" signal > "$evidence/tpcc-signal.log" 2>&1
timeout -k 5s 120s "./$out/replication/tests/tpcc_mixed_test" > "$evidence/tpcc-checkpoint.log" 2>&1
timeout -k 5s 120s "./$out/replication/tests/silo_context_test" > "$evidence/silo-context.log" 2>&1
for threshold in 25 100; do
  python3 -B replication/experiments/run.py "replication/experiments/configs/queued-signal-$threshold.json" \
    --source "$PWD" --dbtest "$out/benchmarks/dbtest" --output "$evidence/pressure-$threshold" \
    --build-info "Linux-signal backend in emulated Linux; GCC 11.4; WORKER_CONTEXTS=1; archived MAC-01 compatibility patch"
  batches=("$evidence/pressure-$threshold"/*/batch.json)
  test "${#batches[@]}" = 1
  batch=${batches[0]%/batch.json}
  python3 -B replication/experiments/analyze.py --output "$evidence/analysis-$threshold" batch "$batch"
  python3 -B replication/experiments/report.py "$evidence/analysis-$threshold/analysis.json" --output "$evidence/report-$threshold" \
    --label "Linux-signal backend in emulated Linux; local functional pressure; monotonic ns" --require-complete
done
sha256sum "$out"/benchmarks/dbtest "$out"/replication/tests/{tpcc_mixed_test,silo_context_test} \
  "$evidence"/{signal,batched_notifications,starvation}-{debug,optimized,ubsan} > "$evidence/artifacts.sha256"
echo 'PASS MAC-09 Linux-signal backend in emulated Linux; MAC-07/MAC-08 signal repetitions'
