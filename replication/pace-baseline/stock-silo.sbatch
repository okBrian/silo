#!/usr/bin/env bash
#SBATCH --job-name=silo-stock
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=01:00:00
#SBATCH --output=silo-stock-%j.log

set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo 'This baseline requires Linux x86-64.' >&2
  exit 2
fi
if [[ -z ${SLURM_JOB_ID:-} && ${SILO_LOCAL_VALIDATION:-0} != 1 ]]; then
  echo 'Submit this job with Slurm. Use SILO_LOCAL_VALIDATION=1 only on a separate Linux test machine.' >&2
  exit 2
fi
job_id=${SLURM_JOB_ID:-local-validation}
if [[ ( $# != 1 && $# != 3 ) || ! -d $1 ]]; then
  echo 'Usage: sbatch stock-silo.sbatch /path/to/silo-input [/path/to/run.py /path/to/stock-config.json]' >&2
  exit 2
fi

source_dir=$(cd "$1" && pwd -P)
experiment_runner=''
if [[ $# == 3 ]]; then
  experiment_runner=$(realpath -- "$2")
  experiment_config=$(realpath -- "$3")
  [[ -r $experiment_runner && -r $experiment_config ]]
  command -v python3
fi
silo_rev=cc11ca1ea949ef266ee12a9b1c310392519d9e3b
masstree_rev=1150824e6a10a0384ba4bfed6b916ce62f96315f
repeats=${SILO_REPEATS:-5}
runtime=${SILO_RUNTIME:-30}
test_timeout=${SILO_TEST_TIMEOUT:-600}
if [[ ! $repeats =~ ^[1-9][0-9]*$ || ! $runtime =~ ^[1-9][0-9]*$ || ! $test_timeout =~ ^[1-9][0-9]*$ ]]; then
  echo 'SILO_REPEATS, SILO_RUNTIME, and SILO_TEST_TIMEOUT must be positive integers.' >&2
  exit 2
fi

results_root=${SILO_RESULTS_ROOT:-${SLURM_SUBMIT_DIR:-$PWD}/results}
mkdir -p "$results_root"
run_dir=$(mktemp -d "$results_root/silo-stock-${job_id}.XXXXXX")
run_dir=$(cd "$run_dir" && pwd -P)
echo "Results directory: $run_dir"
exec > >(tee -a "$run_dir/job.log") 2>&1

work_dir=''
finish() {
  local status=$?
  printf '%s\n' "$status" > "$run_dir/exit-status"
  if [[ -n $work_dir ]]; then rm -rf -- "$work_dir"; fi
}
trap finish EXIT
set -x

for tool in git make g++ gcc autoreconf timeout awk; do command -v "$tool"; done
[[ $(git -C "$source_dir" rev-parse HEAD) == "$silo_rev" ]]
[[ $(git -C "$source_dir/masstree" rev-parse --show-toplevel) == "$source_dir/masstree" ]]
[[ $(git -C "$source_dir/masstree" rev-parse HEAD) == "$masstree_rev" ]]

work_dir=$(mktemp -d "${SLURM_TMPDIR:-${TMPDIR:-/tmp}}/silo-build.XXXXXX")
if [[ $work_dir =~ [[:space:]] ]]; then
  echo 'The Silo Makefile requires a build path without whitespace.' >&2
  exit 2
fi
git clone --no-local --no-checkout "$source_dir" "$work_dir/silo"
cd "$work_dir/silo"
git checkout --detach "$silo_rev"
# Only permit local Git transport for this pinned copy of Masstree.
git config submodule.masstree.url "$source_dir/masstree"
git -c protocol.file.allow=always submodule update --init --checkout masstree
[[ $(git -C masstree rev-parse HEAD) == "$masstree_rev" ]]
# Legacy Make checks the pinned commit's parent and can fetch when it is missing.
git -C masstree rev-parse --verify "$masstree_rev^"

{
  date -u
  hostname
  uname -a
  lscpu
  awk '/^(Cpus_allowed_list|Mems_allowed_list):/' /proc/self/status
  awk '/^MemTotal:/' /proc/meminfo
  g++ --version
  make --version
  if [[ -n $experiment_runner ]]; then
    printf 'job=%s cpus_per_task=%s experiment_config=%s\n' \
      "$job_id" "${SLURM_CPUS_PER_TASK:-4}" "$experiment_config"
  else
    printf 'job=%s cpus_per_task=%s repeats=%s runtime_s=%s scale_factor=2\n' \
      "$job_id" "${SLURM_CPUS_PER_TASK:-4}" "$repeats" "$runtime"
  fi
  git rev-parse HEAD
  git submodule status
} > "$run_dir/environment.txt"

# Keep the stock engine and Masstree index. Use libc and omit the optional MySQL backend.
common=(MODE=perf MASSTREE=1 USE_MALLOC_MODE=0 MYSQL=0)
jobs=${SLURM_CPUS_PER_TASK:-4}
# Keep GCC diagnostics visible without turning warnings from this legacy source into errors.
sed -i 's/ -Werror -O2 / -O2 /' Makefile
# The stock TPC-C job uses ndb-proto2. Exclude the optional BDB adapter
# and legacy link libraries that the remaining benchmark sources do not use.
git apply <<'SILO_NO_BDB_PATCH'
--- a/Makefile
+++ b/Makefile
@@ -149,10 +149,9 @@
 MASSTREE_OBJFILES := $(patsubst masstree/%.cc, $(O)/%.o, $(MASSTREE_SRCFILES))
 
 BENCH_CXXFLAGS := $(CXXFLAGS)
-BENCH_LDFLAGS := $(LDFLAGS) -ldb_cxx -lz -lrt -lcrypt -laio -ldl -lssl -lcrypto
-
-BENCH_SRCFILES = benchmarks/bdb_wrapper.cc \
-	benchmarks/bench.cc \
+BENCH_LDFLAGS := $(LDFLAGS)
+
+BENCH_SRCFILES = benchmarks/bench.cc \
 	benchmarks/encstress.cc \
 	benchmarks/bid.cc \
 	benchmarks/masstree/kvrandom.cc \
--- a/benchmarks/dbtest.cc
+++ b/benchmarks/dbtest.cc
@@ -14,7 +14,6 @@
 #include "../allocator.h"
 #include "../stats_server.h"
 #include "bench.h"
-#include "bdb_wrapper.h"
 #include "ndb_wrapper.h"
 #include "ndb_wrapper_impl.h"
 #include "kvdb_wrapper.h"
@@ -275,12 +274,7 @@
   }
 #endif
 
-  if (db_type == "bdb") {
-    const string cmd = "rm -rf " + basedir + "/db/*";
-    // XXX(stephentu): laziness
-    int ret UNUSED = system(cmd.c_str());
-    db = new bdb_wrapper("db", bench_type + ".db");
-  } else if (db_type == "ndb-proto1") {
+  if (db_type == "ndb-proto1") {
     // XXX: hacky simulation of proto1
     db = new ndb_wrapper<transaction_proto2>(
         logfiles, assignments, !nofsync, do_compress, fake_writes);
--- a/util.h
+++ b/util.h
@@ -21,6 +21,10 @@
 #include "macros.h"
 #include "small_vector.h"
 
+// Make the existing pair formatter visible when format_list is defined.
+template <typename A, typename B>
+std::ostream &operator<<(std::ostream &o, const std::pair<A, B> &p);
+
 namespace util {
 
 // padded, aligned primitives
SILO_NO_BDB_PATCH
# Use the Slurm CPU mask when the upstream pinning test picks CPUs.
git apply <<'SILO_TEST_AFFINITY_PATCH'
--- a/btree.cc
+++ b/btree.cc
@@ -1,4 +1,5 @@
 #include <unistd.h>
+#include <sched.h>
 
 #include <iostream>
 #include <map>
@@ -1099,12 +1100,12 @@
   static atomic<bool> running(true);
   class worker : public btree_worker {
   public:
-    worker(unsigned int thread, testing_concurrent_btree &btr)
-      : btree_worker(btr), thread(thread) {}
+    worker(unsigned int thread, unsigned int cpu, testing_concurrent_btree &btr)
+      : btree_worker(btr), thread(thread), cpu(cpu) {}
     virtual void
     run()
     {
-      rcu::s_instance.pin_current_thread(thread % coreid::num_cpus_online());
+      rcu::s_instance.pin_current_thread(cpu);
       for (unsigned mode = 0; running.load(); mode++) {
         for (size_t i = thread * keys_per_thread;
              running.load() && i < (thread + 1) * keys_per_thread;
@@ -1121,6 +1122,7 @@
     }
   private:
     unsigned int thread;
+    unsigned int cpu;
   };
 }
 
@@ -1128,10 +1130,23 @@
 mp_test_pinning()
 {
   using namespace mp_test_pinning_ns;
+  cpu_set_t allowed;
+  CPU_ZERO(&allowed);
+  ALWAYS_ASSERT(sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
+  vector<unsigned int> cpus;
+  const unsigned int online = coreid::num_cpus_online();
+  for (unsigned int cpu = 0; cpu < online && cpu < CPU_SETSIZE; cpu++)
+    if (CPU_ISSET(cpu, &allowed))
+      cpus.push_back(cpu);
+  ALWAYS_ASSERT(!cpus.empty());
+
   testing_concurrent_btree btr;
   vector<unique_ptr<worker>> workers;
-  for (size_t i = 0; i < nthreads; i++)
-    workers.emplace_back(new worker(i, btr));
+  for (size_t i = 0; i < nthreads; i++) {
+    const unsigned int cpu = cpus[i % cpus.size()];
+    cerr << "mp_test_pinning worker " << i << " selects allowed CPU " << cpu << endl;
+    workers.emplace_back(new worker(i, cpu, btr));
+  }
   for (auto &p : workers)
     p->start();
   sleep(5);
SILO_TEST_AFFINITY_PATCH
git diff -- Makefile benchmarks/dbtest.cc util.h btree.cc > "$run_dir/build-compatibility.diff"
# The upstream LZ4 recipe invokes literal `make`, which breaks the parallel jobserver.
make -j1 -C third-party/lz4 library 2>&1 | tee "$run_dir/build-lz4.log"
# Configure Masstree serially before compiling to avoid concurrent configure recipes.
make -j1 "${common[@]}" DEBUG=1 CHECK_INVARIANTS=1 masstree/config.h 2>&1 | tee "$run_dir/configure-test.log"
make -j"$jobs" "${common[@]}" DEBUG=1 CHECK_INVARIANTS=1 test 2>&1 | tee "$run_dir/build-test.log"

run_upstream_tests() {
  timeout "$test_timeout" ./out-perf.debug.check.masstree/test 2>&1 | tee "$run_dir/test.log"
  printf 'Upstream test command exited successfully.\n' > "$run_dir/test-passed.txt"
}

if [[ ${SILO_TEST_ONLY:-0} == 1 ]]; then
  run_upstream_tests
  exit 0
fi

make -j1 "${common[@]}" masstree/config.h 2>&1 | tee "$run_dir/configure-dbtest.log"
make -j"$jobs" "${common[@]}" dbtest 2>&1 | tee "$run_dir/build-dbtest.log"

if [[ -n $experiment_runner ]]; then
  benchmark_status=0
  python3 "$experiment_runner" "$experiment_config" \
    --source "$PWD" --dbtest "$PWD/out-perf.masstree/benchmarks/dbtest" \
    --output "$run_dir/experiments" --build-info "GCC $(g++ -dumpfullversion); ${common[*]}; see build-dbtest.log and build-compatibility.diff" \
    || benchmark_status=$?
  run_upstream_tests
  exit "$benchmark_status"
fi

run_tpcc() {
  local workers=$1 duration=$2 name=$3
  timeout "$((duration + 180))" ./out-perf.masstree/benchmarks/dbtest \
    --db-type ndb-proto2 --bench tpcc --scale-factor 2 --num-threads "$workers" \
    --runtime "$duration" --verbose > "$run_dir/$name.stdout" 2> "$run_dir/$name.stderr"
}

run_tpcc 1 5 smoke
printf 'workers,repeat,runtime_s,warehouses,throughput_ops_s,persist_throughput_ops_s,avg_attempt_latency_ms,avg_persist_latency_ms,aborts_s\n' > "$run_dir/stock-tpcc.csv"
for ((rep=1; rep<=repeats; rep++)); do
  workers_order=(1 2)
  if (( rep % 2 == 0 )); then workers_order=(2 1); fi
  for workers in "${workers_order[@]}"; do
    name="tpcc-w${workers}-r${rep}"
    run_tpcc "$workers" "$runtime" "$name"
    awk -v w="$workers" -v r="$rep" -v t="$runtime" '
      NF == 5 {
        for (i = 1; i <= 5; i++)
          if ($i !~ /^[0-9]+(\.[0-9]*)?([eE][+-]?[0-9]+)?$/) next
        if ($1 + 0 > 0) { row = w "," r "," t ",2," $1 "," $2 "," $3 "," $4 "," $5; count++ }
      }
      END { if (count != 1) exit 1; print row }
    ' "$run_dir/$name.stdout" >> "$run_dir/stock-tpcc.csv"
  done
done

# The slow upstream tests run last so a timeout preserves the baseline evidence.
run_upstream_tests
