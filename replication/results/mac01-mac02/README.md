# MAC-01 and MAC-02 local replication evidence

November 4, 2026 · **Emulated Linux functional validation** · Branch `erin/local-replication-evidence`

## Guest and source

The initial restricted `colima list` reported `Broken`. With local socket access, `colima list` reported `Running`, direct SSH reached the x86-64 QEMU guest, and the Docker API answered at `unix://$HOME/.colima/docker.sock`. The run reused that profile without a restart. The guest ran Ubuntu 24.04.4, kernel `6.8.0-117-generic`, with two virtual CPUs and 2 GiB configured RAM (1,962 MiB visible). The Apple M5 Pro host ran Colima 0.10.3 and QEMU 11.1.1. [Host and guest inventory](host-environment.log) · [Profile status](profile-status.log)

The build ran in a fresh `linux/amd64` container from cached `ubuntu:22.04` at digest `sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11`. The container had zero host mounts. A tar archive carried both detached checkouts and their Git metadata into the container, where the build ran in a newly created `/work.*` directory. Silo was `8395daba617e83e6805268bda793ab0d5359d63b`. Masstree was initialized from the prior local clone at the gitlink `1150824e6a10a0384ba4bfed6b916ce62f96315f`. The Linux copy verified both revisions and passed `git fsck --no-dangling` for both repositories. [Build environment](environment.log)

The I11 compatibility patch was applied in the isolated copy. It forward-declares the existing pair formatter for GCC and changes the engine build to `-O1` with warnings enabled and `-Werror` removed. The engine build used `MODE=perf MASSTREE=1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1`. [Build-only patch](build-compatibility.diff)

## Commands and raw output

The [validation script](validate.sh) and [command log](validation.log) record package installation, source verification, build flags, both engine tests, the standalone request driver, the Python tests, all three voluntary-context ABI builds, and the smoke and analysis commands. [Package logs](apt.log.gz) · [Build log](build.log.gz) · [Exit status](exit-status)

The first invocation stopped before compilation because Git rejected the host ownership carried in the tar archive. The retry extracted into a fresh directory with `tar --no-same-owner`. [Initial command log](preflight.log.gz)

The original run cloned Silo locally with `git clone --no-local --no-checkout`, checked out the tested commit, configured the submodule URL to the cached clone, and ran `git -c protocol.file.allow=always submodule update --init --checkout masstree`. To recreate the input from this repository and GitHub, run from the Silo root with the existing Colima Docker socket available.

```sh
evidence="$PWD/replication/results/mac01-mac02"
stage=$(mktemp -d)
git clone --no-local --no-checkout "$PWD" "$stage/source"
git -C "$stage/source" checkout --detach 8395daba617e83e6805268bda793ab0d5359d63b
git -C "$stage/source" config submodule.masstree.url https://github.com/kohler/masstree-beta.git
git -C "$stage/source" submodule update --init --checkout masstree
git -C "$stage/source" apply "$evidence/build-compatibility.diff"
COPYFILE_DISABLE=1 tar --no-xattrs --no-acls --no-fflags -C "$stage" -cf "$stage/source.tar" source
export DOCKER_HOST="unix://$HOME/.colima/docker.sock"
container=$(docker create --platform linux/amd64 --pull never \
  ubuntu@sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11 bash /validate.sh)
docker cp "$stage/source.tar" "$container:/source.tar"
docker cp "$evidence/validate.sh" "$container:/validate.sh"
docker start -a "$container"
docker cp "$container:/evidence" "$stage/"
```

## Results

- Both engine tests passed. The mixed workload retained the expected Q2 snapshot checksum across nine write handoffs and verified New-Order and Payment rollback and retry. The context test covered snapshot isolation, nested protection, abort cleanup, and reclamation. [Mixed workload test](tpcc-mixed-test.log) · [Silo context test](silo-context-test.log)
- The standalone driver and all 11 experiment-tool tests passed. The voluntary-context ABI harness passed 1, 10,000, and 1,000,000 round trips in each of the debug, optimized, and UndefinedBehaviorSanitizer builds. [Request driver](request-driver.log) · [Experiment tools](experiment-tests.log) · [Debug ABI](worker-context-debug.log) · [Optimized ABI](worker-context-optimized.log) · [UBSan ABI](worker-context-ubsan.log)
- The unchanged 64-read smoke configuration completed all eight Wait and Cooperative runs. The analyzer reconciled all 16 warm-up and measurement phase summaries. All 8,125 measurement requests committed, including 432 short requests, with zero aborts, rejections, cancellations, or failures. All four pairs matched short-request inputs and relative arrival deadlines, plus the shared Q2 input prefixes. Cooperative made 36 mid-Q2 handoffs across the four measurement phases and served 144 short requests while Q2 was suspended. Wait showed zero same-worker overlap. [Trace checks](reconciliation.log) · [Analysis replay checks](replay-check.log)
- A separate one-worker, one-second Cooperative smoke used the 10,000-read default. Its 713 Q2s completed 843–2,363 eligible reads each and recorded zero interval checks or handoffs. The 64-read setting exercised the scheduling path in this small workload. [Default-interval check](default-interval-check.log) · [Raw trace](default-interval.tsv.gz) · [Phase summary](default-interval.err)

| Policy | Measurement commits | Short commits | Q2 commits | Drain commits | Mid-Q2 handoffs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wait | 4,143 | 216 | 3,927 | 9 | 0 |
| Cooperative | 3,982 | 216 | 3,766 | 7 | 36 |

Measurement commits include requests that finished during the drain. The one-second samples are functional evidence from emulation. Intel UINTR delivery and the native three-policy comparison require a qualified host.

The [batch archive](batch.tar.gz) contains all eight manifests, raw request TSVs and phase logs, and both source diffs. The saved [batch metadata](batch.json) identifies the binary and tested revisions. The archive regenerated the same [analysis CSV](queued-summary.csv) and trace check output. To replay it from the Silo root:

```sh
replay=$(mktemp -d)
tar -xzf replication/results/mac01-mac02/batch.tar.gz -C "$replay"
python3 -B replication/experiments/analyze.py --output "$replay/analysis" batch "$replay/batch"
python3 -B replication/tests/check_cooperative_batch.py "$replay/batch"
```
