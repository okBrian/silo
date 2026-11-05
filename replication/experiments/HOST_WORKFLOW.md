# MAC-05: clean validation and Figure 10 collection handoff

This workflow verifies the implemented Wait and Cooperative paths. Its saved results
are software checks. Full I17 acceptance and I18 measurements follow I12 delivery,
I13 asynchronous preservation, and I14–I16 integration on a qualified Intel host.

## Clean build and two-policy smoke

Use an x86-64 Linux host with glibc. Ubuntu 22.04, GCC 11.4 and glibc 2.35 are the
locally tested combination. Install the same prerequisites as MAC-01:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends build-essential autoconf automake \
  git python3 libnuma-dev libdb++-dev zlib1g-dev libaio-dev libssl-dev
```

After transferring a committed candidate and this workflow, run from its Silo root:

```bash
git config submodule.masstree.url https://github.com/kohler/masstree-beta.git
git submodule update --init --checkout masstree
test "$(git -C masstree rev-parse HEAD)" = "$(git rev-parse HEAD:masstree)"
evidence_parent=$(mktemp -d)
MAC05_ENVIRONMENT='native Intel Linux software smoke; host qualification recorded separately' \
  bash replication/experiments/validate-local.sh "$PWD" "$evidence_parent/validation"
```

The script clones the specified source HEAD into a new temporary directory, initializes
Masstree from the supplied pinned checkout, applies the archived GCC compatibility
patch in that copy, runs `make clean`, and builds `dbtest`, `tpcc_mixed_test` and
`silo_context_test` with `WORKER_CONTEXTS=1`. It runs both engine tests, the standalone
driver, the experiment-tool suite, and the eight-run paired smoke (one/two workers,
two repetitions, 64-read Cooperative interval). Do not interpret the environment
label as hardware qualification. This script runs no UINTR test or Preemptive policy.

The original input checkout is preserved. The script rejects modified tracked C++ or
assembly inputs and requires a clean, initialized Masstree. Commit engine changes
before using it. The workflow tools and their hashes, exact build patch, environment,
build log, validation log, exit status, manifests, TSVs, and phase summaries are saved.
The default build is the known local `-O1` compatibility build, with `-Werror` removed;
record that choice on Intel too. Any later optimization change needs a named candidate
and a fresh common binary and validation across policies.

`analysis/` contains the analyzer's CSV/JSON plus `report.md`, `per-type.csv`, and
`run-health.csv`. The report gate rejects incomplete, failed, or unreconciled runs.
The trace checker also requires matched short inputs and relative deadlines, matching
Q2 input prefixes, zero Wait overlap, and actual Cooperative mid-Q2 service. Every
planned invocation retains its exit status, including failures. A run without a
matching terminal trace has unknown unaccounted work, never an assumed zero.

At the end, the script archives the batch, extracts it into a new location, regenerates
analysis, and compares all three CSVs and the report byte for byte. JSON includes
relocated input paths, so it is not compared byte for byte. Keep `batch.tar.gz`,
`tools/`, `build-compatibility.diff`, and the source revision together when transferring
evidence. Smoke counts are too small and emulation too slow for native speed claims.

## Replay without a Linux guest or dbtest

From the Silo root, the following uses only Python's standard library. Set `batch`
to the extracted directory containing `batch.json`.

```bash
replay=$(mktemp -d)
tar -xzf replication/results/mac05-workflow/batch.tar.gz -C "$replay"
batches=("$replay"/queued-cooperative-smoke-*)
batch="${batches[0]}"
python3 -B replication/experiments/analyze.py --output "$replay/analysis" batch "$batch"
python3 -B replication/experiments/report.py "$replay/analysis/analysis.json" \
  --output "$replay/analysis" --require-complete \
  --label 'Emulated Linux functional validation; one-second smoke estimates'
python3 -B replication/tests/check_cooperative_batch.py "$batch"
```

The same commands accept the MAC-02 archive, whose extracted directory is `batch`.
Use the saved `tools/` copies if future analysis code changes. The archive includes
per-request timestamps, attempt/abort/outcome counters, cooperative reads/checks/yields,
Q2 checksums, run manifests, batch metadata and both repository diffs.

## Figure 10-style native run plan

[figure10.json](plans/figure10.json) is a planning artifact, **not executable runner
configuration**. It names pending dependencies and leaves source/binary and CPU IDs
unset. The current CLI and runner reject Preemptive. Their CPU list restricts the
whole process; it does not assign individual workers or the sender to separate cores.
The existing reversal of variants is enough to vary the two-policy smoke order. It
cannot express the planned three-policy schedule below. Implement and validate those
capabilities before materializing a runnable final configuration; do not substitute a
Wait run for Preemptive or record process affinity as worker placement.

1. Run the [I03 inventory and I12 delivery kit](../uintr/README.md). Save CPU model,
   topology, allowed mask, kernel/interface, CPUID and receiver-resumption evidence.
   Select 16 physical worker cores on one socket and a distinct physical sender core;
   exclude their SMT siblings. Record the exact CPU IDs and NUMA memory placement.
   If fewer than 17 physical cores are available, create a separately named reduced
   configuration and report the deviation. Do not silently reuse the 16-worker label.
2. Finish the I13–I16 native checks and full I17 consistency, pressure, reclamation,
   shutdown and stress validation. The MAC-05 software suite is only part of I17.
   Freeze one source revision, common binary/hash, compiler/flags, kernel, interrupt
   setup, logging choice and machine settings for all three policies. Record loaded
   modules/governor/turbo/SMT, other load, memory limits and CPU affinity. Retain any
   local patches and untracked build inputs. The MAC-05 `-O1` build is the tested
   starting recipe, not an assertion that it matches the paper's optimization.
3. Run a matched native pilot before the 16-worker series: one/two workers with a
   separate sender, all three policies, fresh datasets and the same arrival settings
   within each comparison. Inspect eligible Q2 reads, checks, yields, interrupts,
   actual switches, Q2 commits and tail sample counts. The small local Q2s have fewer
   than 10,000 eligible reads; zero Cooperative handoffs at the main default is a
   result to retain. Keep the 64-read smoke separate. Any workload/dataset/interval
   adjustment becomes a new named configuration with all policies rerun.
4. Use 16 workers/warehouses, long corrected `q2_min_stock_v1`, 50/50 New-Order/Payment,
   100 suppliers scanned per Q2, high batch/global pending capacity 64, high local
   pending capacity 4 and low local pending capacity 1 per worker. Send high batches
   every 1 ms (64,000 offered shorts/s), independent of completions; keep fixed routing,
   rejection, retry limit 100, all loader seeds and remote-input rules matched.
   Use 5 s warm-up, 30 s measurement and up to 30 s drain for each phase. Reset and
   reload for each invocation. The planned wall timeout is 900 s including loading;
   increase it under a new recorded configuration if the pilot needs more setup time.
5. Run five independent repetitions, with measurement seeds 1–5 shared across policy
   triplets (and the runner's derived warm-up seed). Use the following order, retaining
   failures in place. Five repetitions vary order but do not fully balance it; a fully
   balanced six-repetition extension must be recorded as a configuration change.

| Repetition / seed | First | Second | Third |
| --- | --- | --- | --- |
| 1 | Wait | Cooperative | Preemptive |
| 2 | Cooperative | Preemptive | Wait |
| 3 | Preemptive | Wait | Cooperative |
| 4 | Wait | Preemptive | Cooperative |
| 5 | Preemptive | Cooperative | Wait |

The Cooperative main interval is 10,000 reads. Preemptive uses the 100% starvation
threshold after its policy integration is validated; the current Wait/Cooperative
manifests correctly have no active starvation threshold. Record notifications, handler
deliveries, switches, attempts and commits separately. Save each per-request trace,
phase summary, exact argument vector, timing, exit state and manifest before analysis.

## Report acceptance and differences from the paper

Recover New-Order, Payment and Q2 p50/p90/p99/p99.9 for end-to-end, scheduled and
queue latency, with global/local queue components, plus per-type window throughput,
attempts/aborts, generated/rejected/cancelled/user-aborted/retry-exhausted/failed counts,
drain commits and outstanding work at cutoff. Verify terminal accounting at exit;
retain failed/incomplete runs with their missing metrics and issues. `per-type.csv`
and `run-health.csv` demonstrate these fields on the local smoke. The report's
`not_committed` is all noncommitted terminal outcomes, not a count of missing records.

Do not pool independent repetitions by default. Preserve every repetition's nearest-rank
percentiles and counts; summarize percentile medians/ranges and throughput mean/sample
SD across successful matched repetitions, showing the successful/planned denominator.
Flag p99.9 below 1,000 samples; reaching 1,000 is not a guarantee of tail precision.
Make final latency panels per transaction type and percentile with all three policies,
and include throughput, offered/completed rates and abort/failure tables. Current
`plot.py` shows only overall p50/p99 diagnostics and is not the final Figure 10 plotter.

Record that the paper uses ERMIA and a Xeon Gold 6448H while this port uses Silo/Masstree
and a separately qualified Intel host. Fixed arrival deadlines, bounded rejection,
corrected Q2 minimum selection, exact eligible-read interval, warm-up and full terminal
sampling are project choices in the [experiment contract](../docs/experiment-contract.md).
Keep the native Silo comparison, authors' artifact reference and Mac emulation results
separate. Remaining rubric coverage and limited sensitivity experiments belong to I18;
no native three-policy result is claimed by MAC-05.
