#!/bin/bash
set -euxo pipefail
cd /mac08-source
evidence=/mac08-evidence
exec > >(tee "$evidence/pressure-recovery.log") 2>&1
trap 'status=$?; printf "%s\n" "$status" > "$evidence/pressure-exit-status"' EXIT
cp "$evidence/exit-status" "$evidence/validation-attempt1-exit-status"
cp replication/tests/validate_starvation.sh "$evidence/validation-attempt1.sh"
for threshold in 25 100; do
  if [[ "$threshold" = 100 ]]; then
    python3 -B replication/experiments/run.py "replication/experiments/configs/queued-starvation-$threshold.json" \
      --source "$PWD" --dbtest out-perf.masstree.contexts/benchmarks/dbtest --output "$evidence/pressure-$threshold" \
      --build-info "GCC 11.4; WORKER_CONTEXTS=1; MYSQL=0; USE_MALLOC_MODE=0; archived MAC-01 -O1 compatibility patch"
  fi
  batches=("$evidence/pressure-$threshold"/*/batch.json)
  test "${#batches[@]}" = 1
  batch=${batches[0]%/batch.json}
  python3 -B replication/experiments/analyze.py --output "$evidence/analysis-$threshold" batch "$batch"
  python3 -B replication/experiments/report.py "$evidence/analysis-$threshold/analysis.json" --output "$evidence/report-$threshold" \
    --label "MAC-08 emulated x86-64 Linux functional pressure test; monotonic ns budget" --require-complete
 done
out=out-perf.masstree.contexts
sha256sum "$out"/benchmarks/dbtest "$out"/replication/tests/{tpcc_mixed_test,silo_context_test} "$evidence"/starvation-{debug,optimized,ubsan} > "$evidence/artifacts.sha256"
echo 'PASS all MAC-08 runtime checks and four pressure runs; corrected analysis commands; signal repeat pending MAC-09'
