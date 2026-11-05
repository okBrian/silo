#!/bin/bash
# Focused follow-up after validate_signal_backend.sh: runtime is unchanged;
# strengthen deterministic low-queue/reset barriers and real paused abort probes.
# The disposable source already has the archived engine compatibility patch.
set -euxo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
test $# = 1
mkdir -p "$1"
evidence=$(cd "$1" && pwd)
exec > >(tee "$evidence/validation.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT
sha256sum benchmarks/{request_driver.*,worker_context.*,signal_backend.*} \
  replication/tests/{batched_notification_cases.h,batched_notifications_test.cc,starvation_cases.h,starvation_test.cc,tpcc_mixed_test.cc,validate_signal_fixtures.sh} > "$evidence/source.sha256"
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -pthread -DSILO_WORKER_CONTEXTS)
runtime=(benchmarks/request_driver.cc benchmarks/worker_context.cc benchmarks/signal_backend.cc)
for build in debug optimized ubsan; do
  case "$build" in
    debug) opt=(-O0 -g) ;;
    optimized) opt=(-O3) ;;
    ubsan) opt=(-O2 -fsanitize=undefined -fno-sanitize-recover=all) ;;
  esac
  for suite in batched_notifications starvation; do
    c++ "${flags[@]}" "${opt[@]}" "${runtime[@]}" "replication/tests/${suite}_test.cc" -o "$evidence/$suite-$build"
    timeout -k 5s 60s "$evidence/$suite-$build" signal > "$evidence/$suite-$build.log" 2>&1
  done
done
for repetition in {1..20}; do
  for suite in batched_notifications starvation; do
    timeout -k 5s 60s "$evidence/$suite-optimized" signal > "$evidence/$suite-repeat-$repetition.log" 2>&1
  done
done
for suite in batched_notifications starvation; do
  timeout -k 5s 60s "$evidence/$suite-optimized" > "$evidence/$suite-checkpoint.log" 2>&1
done
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 tpcc_mixed_test > "$evidence/build.log" 2>&1
out=out-perf.masstree.contexts
timeout -k 5s 120s "./$out/replication/tests/tpcc_mixed_test" signal > "$evidence/tpcc-signal.log" 2>&1
timeout -k 5s 120s "./$out/replication/tests/tpcc_mixed_test" > "$evidence/tpcc-checkpoint.log" 2>&1
sha256sum "$out/replication/tests/tpcc_mixed_test" "$evidence"/{batched_notifications,starvation}-{debug,optimized,ubsan} > "$evidence/artifacts.sha256"
echo 'PASS final signal/checkpoint fixtures; 53 real aborted write probes while Q2 is paused'
