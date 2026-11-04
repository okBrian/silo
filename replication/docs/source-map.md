# I02 source map

Inspected November 3, 2026. This audit defines the integration boundary for I06–I16. Runtime validation belongs to those tasks. Revision and retrieval details are in [provenance](provenance.md).

| Source | Pinned revision | Scope inspected |
|---|---|---|
| Silo | `cc11ca1ea949ef266ee12a9b1c310392519d9e3b` | Benchmark dispatch, TPC-C, transaction state, RCU and ticker |
| Silo Masstree | `1150824e6a10a0384ba4bfed6b916ce62f96315f` | Gitlink verified. Submodule remains uninitialized locally. Silo wrapper inspected |
| PreemptDB | `55823979233ecd833f1f4e10848de5e9bc0af667` | Public source downloaded and inspected, including its tracked Masstree files |

PreemptDB paths below refer to that exact revision. Its Masstree directory is tracked in the reference tree, independently of Silo's Masstree gitlink. The public revision postdates publication, so correspondence to the exact Figure 10 executable remains unverified. [Pinned reference tree](https://github.com/sfu-dis/preemptdb/tree/55823979233ecd833f1f4e10848de5e9bc0af667)

| Mechanism | Inspected PreemptDB source and symbols | Silo integration boundary / required work |
|---|---|---|
| Context state | `uintr.h` `pcontext`, `NUM_CONTEXTS`; `dbcore/sm-thread.{h,cpp}` main/preemptive contexts, stack and shadow thread | I07 context module, I08 explicit context-owned state. Reference stores stack bounds, RSP, FS/GS, an 8192-byte XSAVE area and starvation counters |
| Switching | `uintr.cc` `init_stack`, `swap_context`, `swap_helper`, `pcontext::xsave/xrstor` | Reference uses inline x86 assembly, FS/GS switching and UINTR instructions even for voluntary switches. I07 needs a voluntary path that works without UINTR and separate ABI validation |
| Handler | `uintr.cc` `interrupt_handler_func`, `handler_helper`; `uintr.h` registration wrappers | I12 delivery and I13 preservation tests precede I14 integration. Handler avoids the switch instruction range and returns immediately when protection is active |
| Protected regions | `uintr.cc` `lock_counter`, `pcontext::lock/unlock`; allocation wrappers in `uintr.h`; `masstree/masstree_get.hh`, `masstree_insert.hh`, `masstree_remove.hh`, `masstree_struct.hh` | I08 must audit Silo locks, allocation, callbacks, commit/abort and reclamation. Reference counters and shadow TLS cannot be assumed safe for Silo |
| Queues | `benchmarks/bench.h` main/preemptive/global queues; `dbcore/ring-buff-queue.h` `OneToOneQueue` | I06 explicit single-producer/single-consumer ownership and acquire/release publication. Reference ring uses volatile indices, which do not establish C++ inter-thread synchronization |
| Admission and batching | `benchmarks/tpcc/tpcc-sequential.cc` `tpcc_sequential_worker::MyWork`, scheduler-thread branch | I06 independent generation, I15 batch notifications. Reference fills global queue, distributes to worker queues and sends UINTR after visiting a worker |
| Dispatch and retry | Same `MyWork`; `benchmarks/bench.cc` `do_workload_function`, `do_preemptive_transaction`, `finish_workload` | I10–I14 share request identity and terminal accounting. Reference preemptive loop pops before executing and contains an unfinished aborted-transaction retry path |
| Starvation | `uintr.cc` `reset_timer`, `add_preempted_time`, `starved`; admission in `MyWork`; batch loop in `do_preemptive_transaction` | I16 accounts high-priority cycles over elapsed cycles since main-context timer reset, checking admission and between high-priority requests |
| Configuration | `benchmarks/dbtest.cc`, `dbcore/sm-config.cpp`, `CMakeLists.txt` | Record explicit settings. CLI defaults, README example and paper settings differ |
| Workload and timing | `benchmarks/tpcc/tpcc-sequential.cc`, `tpcc-config.{h,cc}`, `tpcc.h`; `util.h` timer; `benchmarks/bench.cc` | I09 schema and adapters, I05/I06 timing. Reference queues carry workload index plus timer, while worker execution generates transaction inputs |

Silo's `benchmarks/dbtest.cc` selects `--bench tpcc`. `benchmarks/bench.cc` `bench_worker::run()` selects and executes transactions. Its retry loop restores the RNG seed but restarts the successful-attempt timer. The five output columns are aggregate throughput, persistent throughput, average latency in milliseconds, average persistence latency in milliseconds, and aggregate abort rate. The queued path will require an explicit opt-in at this dispatch boundary.

Silo `benchmarks/bench.h` owns `txn_obj_buf`, `arena`, RNG and backoff on each worker. `benchmarks/tpcc.cc` supplies New-Order, Payment, table loaders and warehouse selection. `rcu.h` `scoped_rcu_base` shares per-thread synchronization depth, and `ticker.h` guards update per-core depth/epoch state. `masstree_btree.h` scan wrappers use RCU guards. `txn_proto2_impl.{h,cc}` couples transaction epochs, per-core state and persistence. I08 must keep reclamation protection covering every suspended transaction and prevent a second context from reentering a held lock. Moving only the transaction buffer or stack is insufficient.

## Q2 behavior and port decision

The audited function is `tpcc_sequential_worker::txn_query2()` in `benchmarks/tpcc/tpcc-sequential.cc`, lines 888–1044. It opens a transaction, scans five regions and 62 nations, randomly chooses one region, joins its nations to suppliers, and reads each supplier's stock list. `tpcc-config.h` builds that list in warehouse/item order using `(warehouse_id * item_id) % 10000`. The supplier loader creates 10,000 rows, while `tpcc_nr_suppliers` defaults to inspecting the first 100 supplier IDs. Items number 100,000 per the workload constants. Q2 then reads an item for its chosen stock row, skips items whose data contains lowercase `b`, and commits. Sorting and result printing are unfinished. [Pinned Q2 source](https://github.com/sfu-dis/preemptdb/blob/55823979233ecd833f1f4e10848de5e9bc0af667/benchmarks/tpcc/tpcc-sequential.cc#L888)

The minimum-selection loop initializes `min_qty` to the maximum int16 value and never updates it. Consequently, with ordinary stock quantities it selects the last eligible stock entry, despite the minimum-stock comment. An empty mapping leaves item ID zero. The two scanners are static thread-local objects tied to an arena. These are concrete port concerns for I09.

Project decision for I09 is to implement the intended minimum-stock selection, retain the inspected joins, supplier range, mapping order and actual lowercase-`b` exclusion, and expose a deterministic result checksum for validation. On equal quantities retain the first mapping entry. Skip suppliers with empty stock lists. Name this variant `q2_min_stock_v1` and record the minimum/empty-list fixes as deviations from this public revision. Use context-owned scanners. Tiny tests must cover unequal quantities, ties, empty lists, region/nation filtering and the item filter. A source-behavior oracle and a corrected minimum oracle must be distinguished. No claim of full TPC-H Q2 compliance follows from this variant.

## Request-generation and policy findings

The reference scheduler replenishes low-priority work whenever a worker queue has room. High-priority generation fills the global queue on a timer and drains it round-robin, filling each visited worker queue. Its next deadline is computed from the current time. The drain loop has no next-arrival deadline check and can wait on full queues or starvation until the batch drains or `running` becomes false. Thus overload changes the offered arrival schedule. Timers are created per request, and `util.h` uses `gettimeofday` for latency, while arrival scheduling uses `high_resolution_clock`.

The reference Wait path finishes its current transaction and favors queued high-priority work at transaction boundaries. Cooperative Q2 increments a counter for stock reads and, with `YIELD_ALL=1` (the CMake default), supplier/item reads too. It switches when the count is **greater than** `switch_threshold`, giving threshold + 1 accesses. The CLI default is 10. `do_preemptive_transaction()` runs high-priority work until the queue is empty or the main context is starved. Its retry TODO and volatile queue indices are reasons to implement the explicit [experiment contract](experiment-contract.md).

The paper's 88–96% result compares against **Wait**. The existing research plan already records that correction. The original proposal LaTeX remains unavailable. [Paper, Section 6.3](https://www.cs.sfu.ca/~tzwang/preemptdb.pdf)
