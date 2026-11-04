# I08 Silo context state and protected regions

November 3, 2026. Silo source base `8b6db36123a24958c97589adf63516cdc68bc30c` plus I08. Masstree gitlink `1150824e6a10a0384ba4bfed6b916ce62f96315f`.

## Source audit

| State or operation | Ownership and switching rule |
|---|---|
| Benchmark placement buffer, string arena and RNG | One [`bench_txn_context`](../../../benchmarks/bench_txn_context.h) per logical context. Silo's adapter sizes the full placement buffer, including the aligned header. The queued New-Order and Payment adapters receive their context explicitly and seed its RNG for each attempt. |
| TPC-C temporary keys, values, scanner results and retry inputs | Scratch strings come from the executing context's arena. Scanner objects and locals remain on the executing stack. Stock Delivery's cursor and stock timestamp counter stay with the single-context worker. Queued timestamps come from the request ID. Retry bookkeeping lives with the request and its executing stack. |
| Worker identity, per-core commit IDs and persistence | The alternate stack runs on the same pthread with the same Silo core ID. High commits keep using the stock per-core TID sequence and epoch rules. A live transaction blocks switching when persistence is enabled, because logging can wait on progress tied to the worker's tick. The optional tuple/node lock-ownership diagnostic TLS also requires whole-transaction protection. |
| Transaction construction, writable attempts, commit and abort | The adapter guards each call through cleanup. A writable attempt stays protected from construction through destruction, since a successful insert can hold its tuple lock between calls. A failed commit leaves the object live for the caller's existing abort step. The optional TPC-C partition locks have their own guard through lock release, including after transaction destruction. |
| Masstree cursors, scan callbacks, and shared allocator | Masstree's `simple_threadinfo` and scan cursors are local to each operation. The adapter guards point operations and the full forward/reverse scan, including callbacks and RCU unwinding. Allocations and frees inside those operations finish within the guarded call. A switch point belongs after the adapter returns, with benchmark-side shared state and locks settled. |
| RCU, ticker and protocol GC | A paused snapshot keeps its original `scoped_rcu_region` alive. Its ticker guard retains the per-core epoch lock, so the reclamation frontier cannot pass that tick. A high transaction's guard nests in the same worker state. Completing high work drops its own depth while the snapshot still pins the epoch. The outermost release restores the existing RCU and protocol cleanup path. Allocator freelists, RCU queues and protocol cleanup state stay worker-owned. |

The supported handoff is from an audited point on the alternate stack while a logging-disabled protocol-2 snapshot is live, or at an attempt boundary. The original stack completes its short attempt and exits its protected scopes before resuming the snapshot. The scheduler must avoid epoch synchronization or persistence waits while a snapshot is suspended. Both contexts share the worker's reclamation horizon, so holding a snapshot for a long time also delays reclamation elsewhere.

[`ScopedNonPreemptible`](../../../preemption.h) keeps a per-pthread nesting count. [`WorkerContext::checkpoint()`](../../../benchmarks/worker_context.h) consumes a coalesced request only at depth zero. Scope exit restores depth without transferring control from a destructor. The worker polls at the next eligible checkpoint or attempt boundary, outside exception handling. The original stack checks the queue after either a yield or completion. It can rearm the request before resuming the alternate stack if work remains. The API's notifications are same-thread. Queue publication, cross-thread notification and scheduling policy belong to I10–I11. Asynchronous handler and transition safety belong to I13–I14.

## Reproduce

Build on Linux x86-64 with glibc and the pinned Masstree source. Apply the [I04 build compatibility changes](../../pace-baseline/README.md) to a temporary build copy if required by the compiler or installed backends.

```sh
make -j1 -C third-party/lz4 library
make -j1 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 masstree/config.h
make -j2 MYSQL=0 USE_MALLOC_MODE=0 WORKER_CONTEXTS=1 silo_context_test dbtest
./out-perf.masstree.contexts/replication/tests/silo_context_test
```

For an invariant build, use `DEBUG=1 CHECK_INVARIANTS=1` on both top-level Make commands and run the executable under `out-perf.debug.check.masstree.contexts/`. The context build enables automatic engine protection. Ordinary builds compile out that instrumentation. The current stock and queued benchmark loops remain single-context. The engine harness runs the two-stack path directly through the same Silo adapter and context storage.

## Local validation

The harness ran on emulated Linux x86-64 with GCC 11.4 and glibc 2.35. The temporary build copy used the I04 GCC pair formatter and Berkeley DB compatibility changes. The optimized build used `-O1` with warnings enabled. [Build and test output](linux-x86-64.log)

| Check | Result |
|---|---|
| Live snapshot and context isolation | The original stack committed a larger value while the alternate stack was paused. The snapshot resumed with its original value, arena strings and RNG state. |
| Nested protection and deferred work | Checkpoints inside a forward scan and an inner guard both deferred service. Service ran after the scan. The reverse scan threw through the adapter, released its guard, and the low attempt aborted before yielding. A live insert blocked both checkpoint and direct yield until abort. |
| Failed commit | A different pthread changed a record in the active transaction's read set. Commit failed, the caller aborted, and protection and RCU depth returned to zero. |
| Reclamation | A real RCU callback retired a value while the snapshot was paused. The ticker attempted to advance, the snapshot still read the value, and the callback freed it after the snapshot's guard ended. |
| TPC-C smoke | The stock dispatch produced five finite metrics. The queued dispatch committed all 72 warm-up and all 72 measured requests. Request IDs, both transaction types, worker routing, timestamps and phase counts reconciled. [Stock output](stock.out) · [Queued records](queued.tsv) · [Phase summaries](queued.err) |
| Existing component tests | The optimized I07 ABI and lifecycle harness passed one million round trips. The I06 standalone driver and all eight I05 experiment-tool tests passed. |

The engine harness passed four optimized runs and one debug run with Silo and Masstree invariants enabled. The TPC-C smokes use the ordinary dispatch loops in the context-enabled build and are functional checks. Native PACE execution and asynchronous switching validation remain open.
