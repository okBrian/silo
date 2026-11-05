#include "../../benchmarks/signal_backend.h"
#include "../../preemption.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <chrono>

static void check_at(bool ok, int line) { if (!ok) { std::fprintf(stderr, "FAIL signal backend line=%d\n", line); std::abort(); } }
#define check(value) check_at((value), __LINE__)
struct Test {
  SignalBackend signal;
  unsigned corrupt;
  bool unwound = false;
  std::atomic<bool> ready{false};
  unsigned captures = 0;
  explicit Test(unsigned value) : corrupt(value) {}
  static void run(WorkerContext &context, void *raw) {
    Test &t = *static_cast<Test *>(raw);
    struct Cleanup { bool &done; ~Cleanup() { done = true; } } cleanup{t.unwound};
    // Pending repeated notifications coalesce while protected. Neither nested
    // protection nor leaving it alone may switch. Delivery is observed here.
    {
      ScopedNonPreemptible outer, inner;
      t.signal.send(); t.signal.send();
      t.signal.window(context, false);
      check(t.signal.captures() == 0 && t.signal.handlers() == 1 && t.signal.deferred() == 1);
    }
    t.ready = true;
    if (t.corrupt) t.signal.corrupt_next_capture(t.corrupt);
    // Sender is independent. Long pure assembly windows allow delivery at an
    // arbitrary loop instruction, rather than only the unblock syscall return.
    while (t.signal.captures() < (t.corrupt ? 1u : 64u))
      t.signal.window(context, true, 1000000);
    t.captures = t.signal.captures();
    throw std::runtime_error("expected cleanup after resumed computation");
  }
};

int main() {
  for (unsigned corrupt : {0u, 1u, 2u}) {
    Test test(corrupt);
    WorkerContext context(Test::run, &test);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> requests{0};
    // Only this sender calls send() concurrently with the low context. The two
    // protected sends above finish before releasing it at the first handoff.
    std::atomic<bool> start{false};
    std::thread sender([&] {
      while (!start.load() || !test.ready.load()) std::this_thread::yield();
      while (!stop.load()) {
        test.signal.send();
        ++requests;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    });
    // An initial pending notification starts capture. After that, an independent
    // sender repeatedly interrupts running arithmetic and high-service pauses.
    test.signal.send();
    bool caught = false;
    unsigned switches = 0;
    try {
      // Start sender only once the low context has consumed its protected event.
      start = true;
      while (context.resume()) {
        ++switches;
        sigset_t mask;
        check(pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0 && sigismember(&mask, SIGUSR2) == 1);
        // Clobber FP/SIMD state on high service before kernel restoration.
        asm volatile("pxor %%xmm0,%%xmm0\n\tpxor %%xmm15,%%xmm15\n\tfninit" ::: "xmm0", "xmm15");
      }
    } catch (const std::runtime_error &) { caught = true; }
    stop = true;
    sender.join();
    check(caught && test.unwound && switches == test.captures);
    check(test.signal.continuation_ok() == !corrupt);
    if (!corrupt) check(test.signal.interior_captures() > 0);
    std::printf("PASS linux-signal corrupt=%u handlers=%u captures=%u interior_captures=%u switches=%u sender_requests=%u exact_continuation=%u cleanup=1\n",
      corrupt, test.signal.handlers(), test.captures, test.signal.interior_captures(), switches, requests.load(), !corrupt);
  }
}
