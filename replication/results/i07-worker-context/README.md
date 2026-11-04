# I07 voluntary context switching

November 3, 2026. Silo source base `70f5c73a9dcd368c2717af52ad0298ff2b8f007e` plus the I07 changes. [WorkerContext interface](../../../benchmarks/worker_context.h) · [Implementation](../../../benchmarks/worker_context.cc)

## Lifecycle and platform

Construct one pair on the worker's original pthread stack with an entry function and an argument that stays live until the entry completes. The pair owns a 1 MiB writable alternate stack with an inaccessible page below it. The pthread owns the original stack. The pair remains at a stable address for the life of both contexts.

1. Call `resume()` on the original stack to enter the alternate stack.
2. Call `yield()` inside the entry to return control to `resume()`, which returns `true`. The next `resume()` continues at the same `yield()` call.
3. When the entry returns, the outstanding `resume()` returns `false`. An uncaught entry exception unwinds on the alternate stack and is rethrown to the caller on the original stack.
4. Destroy an unused or finished pair on the original stack to release the mapping. Shutdown of a suspended pair requires resuming the entry so it can return and destroy its locals.

The implementation targets C++11 and the SysV AMD64 LP64 ABI on Linux with glibc and the default non-shadow-stack configuration. It uses glibc's `getcontext`, `makecontext`, and `swapcontext` without copying PreemptDB assembly. glibc establishes the initial stack alignment and continuation, preserves the synchronous ABI state, and changes the pthread's signal mask via a system call during each swap. The alternate context starts with the mask and floating-point environment captured at construction. Both contexts share pthread identity and TLS. Call sites must be on the constructing thread, outside signal handlers and outside C++ catch handlers and unwinding. The interface supports one active pair per thread. [glibc stack setup](https://codebrowser.dev/glibc/glibc/sysdeps/unix/sysv/linux/x86_64/makecontext.c.html) · [glibc synchronous switch](https://codebrowser.dev/glibc/glibc/sysdeps/unix/sysv/linux/x86_64/swapcontext.S.html) · [SysV AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)

## Validation

The standalone harness ran in a cached Ubuntu 22.04 compiler container on a Colima/QEMU Linux x86-64 guest on Apple Silicon. The container used GCC 11.4.0 and glibc 2.35. A counted round trip is one original-to-alternate resume and one yield back. Each run made one additional round trip to let the entry finish. [Raw output](linux-x86-64.log)

| Check | Result in debug, optimized and UBSan builds |
|---|---|
| 1 round trip | Passed. Original sum 19, alternate sum 32 |
| 10,000 round trips | Passed. Original sum 100,010,017, alternate sum 150,015,029 |
| 1,000,000 round trips | Passed. Original sum 1,000,001,000,017, alternate sum 1,500,001,500,029 |
| Stack separation and alignment | Original local fell inside pthread stack bounds, alternate local outside them. Assembly checked the callback's 16-byte SysV entry alignment |
| Preserved registers and FP controls | Assembly checked distinct sentinels in RBX, RBP and R12–R15 on both sides. The harness checked distinct x87 rounding modes and MXCSR values across the switches |
| Lifecycle and shared thread state | Passed normal completion, exception unwinding, rejected transitions, foreign-thread rejection, shared TLS, and restoration of distinct signal masks |

This evidence applies to explicit calls at ordinary ABI boundaries. An interrupt can arrive with caller-saved registers, flags, SIMD registers and extended state live. I13 must validate saving and restoring that interrupt-time state. I08 must establish transaction-state isolation, protected regions and reclamation safety before live Silo transactions can yield.

## Reproduce

From the Silo root on Linux x86-64 with GCC and glibc:

```sh
set -eu
test_dir=$(mktemp -d)
for flags in '-O0 -g' '-O3 -DNDEBUG' '-O2 -g -fsanitize=undefined -fno-sanitize-recover=all'; do
  c++ -std=c++11 $flags -Wall -Wextra -Werror -pedantic -pthread \
    benchmarks/worker_context.cc replication/tests/worker_context_test.cc \
    replication/tests/worker_context_abi.S -o "$test_dir/worker_context_test"
  "$test_dir/worker_context_test"
done
```
