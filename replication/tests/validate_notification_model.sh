#!/bin/bash
# Disposable initialized x86-64 Linux/glibc source; no native UINTR execution.
set -euxo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
test $# = 1
mkdir -p "$1"
evidence=$(cd "$1" && pwd)
exec > >(tee "$evidence/validation.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/exit-status"' EXIT
git rev-parse HEAD > "$evidence/source-base"
test "$(git -C masstree rev-parse HEAD)" = 1150824e6a10a0384ba4bfed6b916ce62f96315f
{ uname -a; cat /etc/os-release; c++ --version; getconf GNU_LIBC_VERSION; lscpu; taskset -pc "$$"; } > "$evidence/environment.log"
sha256sum benchmarks/{notification_model.h,notification_schedule.h,request_driver.*,worker_context.*,signal_backend.*} \
  replication/tests/{notification_model*,batched_notification*,starvation*,tpcc_mixed_test.cc,validate_notification_model.sh,verify_notification_model.py} \
  > "$evidence/source.sha256"
sha256sum replication/uintr/* > "$evidence/hardware-kit.sha256"
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -pthread -DSILO_WORKER_CONTEXTS)
runtime=(benchmarks/request_driver.cc benchmarks/worker_context.cc benchmarks/signal_backend.cc)
for build in debug optimized ubsan; do
  case "$build" in
    debug) opt=(-O0 -g) ;;
    optimized) opt=(-O3) ;;
    ubsan) opt=(-O2 -fsanitize=undefined -fno-sanitize-recover=all) ;;
  esac
  for suite in notification_model batched_notifications starvation; do
    c++ "${flags[@]}" "${opt[@]}" "${runtime[@]}" "replication/tests/${suite}_test.cc" -o "$evidence/$suite-$build"
    timeout -k 5s 60s "$evidence/$suite-$build" > "$evidence/$suite-$build.log" 2>&1
  done
done
for repetition in {1..20}; do
  for suite in notification_model batched_notifications starvation; do
    timeout -k 5s 60s "$evidence/$suite-optimized" > "$evidence/$suite-repeat-$repetition.log" 2>&1
  done
done
for build in wait cooperative; do
  context=()
  if [[ "$build" == cooperative ]]; then context=(-DSILO_WORKER_CONTEXTS benchmarks/worker_context.cc benchmarks/signal_backend.cc); fi
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic -pthread -O2 "${context[@]}" benchmarks/request_driver.cc \
    replication/tests/request_driver_test.cc -o "$evidence/driver-$build"
  timeout -k 5s 60s "$evidence/driver-$build" > "$evidence/driver-$build.log" 2>&1
done
python3 -B -m unittest discover -s replication/tests -p test_i05_experiments.py -v > "$evidence/experiments.log" 2>&1
git apply replication/results/mac01-mac02/build-compatibility.diff
make -j1 -C third-party/lz4 library > "$evidence/build.log" 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> "$evidence/build.log" 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 tpcc_mixed_test >> "$evidence/build.log" 2>&1
out=out-perf.masstree.contexts/replication/tests/tpcc_mixed_test
timeout -k 5s 120s "./$out" > "$evidence/tpcc-checkpoint.log" 2>&1
# Existing signal mode is a separately labeled regression, never a model event.
timeout -k 5s 120s "./$out" signal > "$evidence/tpcc-signal-regression.log" 2>&1
sha256sum "$out" "$evidence"/{notification_model,batched_notifications,starvation}-{debug,optimized,ubsan} \
  "$evidence"/driver-{wait,cooperative} > "$evidence/artifacts.sha256"
sha256sum -c "$evidence/hardware-kit.sha256" > "$evidence/hardware-kit-verification.log"
python3 -B replication/tests/verify_notification_model.py "$evidence" > "$evidence/reconciliation.log"
echo 'PASS MAC-10 controlled software notification model; native I12 remains prepared'
