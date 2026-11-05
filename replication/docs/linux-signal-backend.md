# Experimental Linux-signal backend

MAC-09 · **Linux-signal backend in emulated Linux**.
This is an opt-in software test backend, selected with `--queued-policy linux-signal`
in an x86-64 Linux/glibc `WORKER_CONTEXTS=1` build. It is not Intel UINTR.
Wait and Cooperative retain their existing selection and scheduling rules.

## Capture and return contract

[SignalBackend](../../benchmarks/signal_backend.cc) reserves SIGUSR2 for the
duration of a run and refuses to replace an existing custom SIGUSR2 handler.
Each worker saves its old mask/alternate stack, blocks signals, installs a guarded
1 MiB signal stack and a separate guarded 1 MiB landing stack, then constructs
its ordinary [WorkerContext](../../benchmarks/worker_context.cc). Both saved
voluntary contexts therefore block signals. A mutex protects sender registration
and teardown; only ordinary sender code uses it. Per-worker `pthread_kill` sends
are separate from modeled transport events, admission and transaction outcomes.

The only capture-eligible instructions are the assembly interval
`silo_signal_begin` through `silo_signal_end`. The window unblocks SIGUSR2 with a
raw Linux syscall, runs bounded arithmetic and blocks it again. It contains no
allocation, locks, logging, TLS-dependent library call or exception machinery.
Other signals remain blocked; SIGKILL/SIGSTOP retain their kernel behavior.
The handler checks the saved RIP and a `volatile sig_atomic_t` eligibility flag.
Protected/ineligible delivery increments a deferral counter and leaves a pending
flag. Ordinary code re-arms that flag before the next eligible window.

On capture, the handler retains the original kernel `ucontext`/FP-state frame on
the signal stack, saves the original RIP/RSP/RDI/mask, blocks signals in the
return frame, and redirects RIP/RSP/RDI to a C landing function on the separate
stack. Its operations are fixed-size stores, signal-safe `sigfillset`, and
saturating `sig_atomic_t` counters. It never calls a glibc context-switch API.
The compiled handler is archived for inspection with the validation evidence.

The handler **returns through the kernel before high service**. The landing
function then calls the ordinary context's `yield()`, allowing the original
worker stack to service shorts. On resumption it restores those four modified
frame fields and invokes the x86-64 signal restorer (`rt_sigreturn`) against the
original complete frame. This avoids glibc `setcontext`'s voluntary-register
contract and avoids guessing/copying the kernel's extended-state frame size.
All signals stay blocked across landing/high-service/restore, so no second
handler can overwrite the retained frame. A frame can be captured only once.
The arithmetic window then continues at its interrupted instruction.

The architecture-specific restorer is intentionally experimental. Its layout
depends on Linux x86-64's signal-frame ABI (RSP points at the ucontext after the
restorer return-address slot). It is neither a portable use of POSIX ucontext
nor a replacement for the native UINTR entry/return implementation.

## State and restrictions

The kernel frame retains GPRs, RIP/RSP, flags, signal mask and FP-state pointer.
Each window checks exact arithmetic continuation and byte-exact legacy FXSAVE
state through byte 415: x87 control/status/stack, MXCSR and XMM0–15. Original
caller FP state is saved/restored around this oracle. Separate negative controls
corrupt the retained R12 or XMM0; each must fail its continuation oracle. Saved
frame restoration also preserves the interrupted stack/red zone without using
it as the handler or landing stack. MAC-04's full GPR/flags/red-zone trap oracle
is supporting evidence; MAC-09's asynchronous oracle directly checks live
arithmetic state, legacy FP/SIMD state and exact completion, rather than claiming
an independent sentinel for every GPR/flag bit. AVX/AVX-512/AMX, CET/shadow stacks,
debug-register state, arbitrary libc interruption and nested captures are not
validated/supported claims of this backend.

Both contexts share a pthread and its TLS. Silo's isolated low/high transaction
storage and paused RCU snapshot lifetime are reused. A signal never redirects
an index operation, transaction write/cleanup, allocator, mutex operation, C++
unwind, or high-service callback. Shared TLS (including errno) is not virtualized;
the audited window has no live TLS-dependent library operation to resume.
Exceptions may unwind after a window has resumed and returned normally. They
may not escape landing or the handler. Mask/stack restoration requires joined
senders; teardown deregisters the worker under the sender mutex, consumes stale
pending SIGUSR2 notifications, then restores its previous stack, disposition
(last user) and mask. Other code must not change this reserved signal while a
backend is active. This opt-in mode also defers other asynchronous signals on
the worker until teardown and is unsuitable for signal-based profiling.

## Silo scheduling and evidence interpretation

The driver opens these windows only at audited completed Q2 reads, after index
protection has returned, with high work queued and the starvation budget allowing
service. A capture is genuinely signal-driven **inside that window**; this does
not establish arbitrary-instruction preemption across the whole Q2 implementation.
Pending signals may instead be delivered at the unblock syscall return. The
standalone sender test separately requires captures inside the arithmetic loop.
If a window receives no usable signal, the queue remains authoritative and the
driver can yield voluntarily. `async_captures` counts only handler redirections;
`switches` includes that explicit fallback. Never infer asynchronous switches
from a publication callback, modeled delivery or pending flag alone.

The repeated MAC-07 fixture retains its independent pending-bit model and event
schedules. The signal policy additionally sends on each actual publication;
model delivery invokes another real signal when the worker remains registered.
Thus model send/delivery/coalescing counts describe the controlled model, not
the real signal send count or hardware coalescing. The two-worker partial-admission
and drain-expiry cases intentionally keep delivery blocked through low completion;
they test signal registration/queue cancellation/teardown at request boundaries
and make zero asynchronous-switch claims. The nine transition cases require
actual captures, intervening short commits and exact same-attempt Q2 continuation.
MAC-08 repeats exact-threshold/recovery/retry/reset/drain cases and three-second
real New-Order/Payment/Q2 pressure at 25% and 100%, with one and two workers.
Budget enforcement remains between whole high requests, with documented overshoot.

## Reproduce

In a disposable initialized Linux source copy with pinned Masstree:

```bash
bash replication/tests/validate_signal_backend.sh /absolute/new-evidence
python3 -B replication/tests/verify_signal_records.py /absolute/new-evidence
```

The validation records source/environment/binary hashes, debug/optimized/UBSan
checks, 20 optimized repeats, original checkpoint/driver regressions, real engine
checks and both pressure configurations. Each executable has a timeout and a
five-second kill fallback. The guest-only archived GCC formatter/`-O1` patch is
applied before the engine build. See [actual results](../results/mac09-signal/README.md).

Linux reference contracts: [signal return](https://man7.org/linux/man-pages/man2/sigreturn.2.html),
[alternate stacks](https://man7.org/linux/man-pages/man2/sigaltstack.2.html),
[signal-safe operations](https://man7.org/linux/man-pages/man7/signal-safety.7.html).
No result here validates native UINTR delivery, UINTR state preservation or the
paper's native performance. Those remain MAC-06/I12–I18.
