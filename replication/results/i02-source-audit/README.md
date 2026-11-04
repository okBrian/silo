# I02 audit evidence

Completed November 3, 2026. This evidence covers source inspection and documentation verification. Runtime acceptance tests are assigned in the [experiment contract](../../docs/experiment-contract.md).

| Command or inspection | Observed result |
|---|---|
| `git status --short` before edits | Clean checkout |
| `git rev-parse HEAD` | `05a9229f4dc8c9e25485e34405b5e01f392b5c65` |
| `git ls-tree HEAD masstree` and `git submodule status` | `1150824e6a10a0384ba4bfed6b916ce62f96315f`, uninitialized locally |
| `git clone --depth 1 https://github.com/sfu-dis/preemptdb.git /private/tmp/silo-i02-preemptdb` | Initial restricted network attempt failed DNS resolution. Approved network retry succeeded |
| `git -C /private/tmp/silo-i02-preemptdb rev-parse HEAD` | `55823979233ecd833f1f4e10848de5e9bc0af667` |
| Reference `git submodule status` | Empty, no submodule entries |
| Source reads with `rg`, `sed`, `cat` | Examined context/switch/handler, protection, queue, admission, retry, Q2/loaders, timer and configuration symbols documented in source map |
| Paper Sections 6.1 and 6.3 | Checked configuration and Wait comparator against author-hosted publication |

The [reference file hashes](reference-files.sha256) cover the 21 inspected reference files. The [verification log](verification.txt) records hash, path, source-baseline and whitespace checks. Every source link uses the pinned revision. The Q2 loop leaves `min_qty` unchanged, the preemptive retry path has an explicit TODO, and the reference arrival loop can stall draining its global batch. These findings informed the documented project choices.

Files added are `docs/source-map.md`, `docs/experiment-contract.md` and this evidence directory. Files updated are `docs/provenance.md`, `README.md`, `IMPLEMENTATION_PLAN.md` and `IMPLEMENTATION_STATUS.md`. The existing research plan already contains the corrected Wait comparator. Original proposal source is still unavailable.

Limitations are local Silo Masstree source availability, exact publication revision correspondence, and pending runtime verification of the future port. Next ready tasks are I06 and the remaining I05 tooling, plus I07 with suitable execution access. Hardware qualification remains I03.
