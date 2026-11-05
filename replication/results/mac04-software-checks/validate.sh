#!/bin/bash
set -euxo pipefail

test "$(uname -m)" = x86_64
uname -smr
cat /etc/os-release
c++ --version
getconf GNU_LIBC_VERSION
taskset -pc "$$"

work=$(mktemp -d)
tar --no-same-owner -xf /source.tar -C "$work"
cd "$work/source"
test "$(git rev-parse HEAD:masstree)" = "$(git -C masstree rev-parse HEAD)"
git rev-parse HEAD
git submodule status
git diff --check
sha256sum benchmarks/{request_driver.cc,request_driver.h,starvation_budget.h} \
  replication/tests/{request_driver_test.cc,worker_context_test.cc,interrupt_state_abi.S,interrupt_state_test.cc}

flags=(-std=c++11 -Wall -Wextra -Werror -pedantic)
for build in debug optimized; do
  case "$build" in
    debug) opt=(-O0 -g) ;;
    optimized) opt=(-O3 -DNDEBUG) ;;
  esac
  c++ "${flags[@]}" "${opt[@]}" -mno-avx \
    replication/tests/interrupt_state_test.cc replication/tests/interrupt_state_abi.S \
    -o "$work/interrupt_state_test"
  timeout 30s "$work/interrupt_state_test"
done

for build in wait cooperative ubsan; do
  opt=(-O2)
  context=()
  if [[ "$build" != wait ]]; then
    context=(-DSILO_WORKER_CONTEXTS benchmarks/worker_context.cc)
  fi
  if [[ "$build" == ubsan ]]; then
    opt+=(-fsanitize=undefined -fno-sanitize-recover=all)
  fi
  c++ "${flags[@]}" "${opt[@]}" -pthread "${context[@]}" \
    benchmarks/request_driver.cc replication/tests/request_driver_test.cc \
    -o "$work/request_driver_test"
  timeout 60s "$work/request_driver_test"
done

c++ "${flags[@]}" -O3 -DNDEBUG -pthread benchmarks/worker_context.cc \
  replication/tests/worker_context_test.cc replication/tests/worker_context_abi.S \
  -o "$work/worker_context_test"
timeout 120s "$work/worker_context_test"

python3 -B -m unittest discover -s replication/tests -p 'test_i05_experiments.py' -v

git apply replication/results/mac01-mac02/build-compatibility.diff
make -j1 -C third-party/lz4 library > /engine-build.log 2>&1
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h >> /engine-build.log 2>&1
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 dbtest tpcc_mixed_test silo_context_test \
  >> /engine-build.log 2>&1
out=out-perf.masstree.contexts
timeout 90s "./$out/replication/tests/tpcc_mixed_test"
timeout 90s "./$out/replication/tests/silo_context_test"
