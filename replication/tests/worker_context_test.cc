#include "../../benchmarks/worker_context.h"

#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <immintrin.h>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>

extern "C" void checked_context_entry(WorkerContext &, void *);
extern "C" bool check_preserved_registers(void (*)(void *), void *, std::uint64_t);

#define CHECK(condition) do {                                            \
  if (!(condition)) {                                                     \
    std::fprintf(stderr, "line %d: %s failed\n", __LINE__, #condition);    \
    std::abort();                                                         \
  }                                                                       \
} while (false)

namespace {

struct RoundTrips {
  std::uint64_t count;
  std::uint64_t alternate_sum = 0;
  std::uintptr_t pthread_stack;
  std::size_t pthread_stack_size;
  std::thread::id owner = std::this_thread::get_id();
};

thread_local unsigned worker_marker = 0;

unsigned x87_rounding()
{
  unsigned short control;
  asm volatile("fnstcw %0" : "=m"(control));
  return control & 0x0c00;
}

bool on_pthread_stack(const RoundTrips &run, const volatile void *address)
{
  const auto value = reinterpret_cast<std::uintptr_t>(address);
  return value >= run.pthread_stack && value - run.pthread_stack < run.pthread_stack_size;
}

void checked_resume(void *arg)
{
  CHECK(static_cast<WorkerContext *>(arg)->resume());
}

void checked_yield(void *arg)
{
  static_cast<WorkerContext *>(arg)->yield();
}

void run_round_trips(std::uint64_t count)
{
  RoundTrips run;
  run.count = count;
  pthread_attr_t attr;
  void *base;
  CHECK(pthread_getattr_np(pthread_self(), &attr) == 0);
  CHECK(pthread_attr_getstack(&attr, &base, &run.pthread_stack_size) == 0);
  CHECK(pthread_attr_destroy(&attr) == 0);
  run.pthread_stack = reinterpret_cast<std::uintptr_t>(base);

  std::fenv_t original_fp;
  CHECK(std::fegetenv(&original_fp) == 0);
  CHECK(std::fesetround(FE_DOWNWARD) == 0);
  const unsigned x87 = x87_rounding();
  const unsigned mxcsr = _mm_getcsr();
  volatile std::uint64_t original_sum = 17;
  CHECK(on_pthread_stack(run, &original_sum));
  worker_marker = 1;

  WorkerContext context(checked_context_entry, &run);
  for (std::uint64_t i = 1; i <= count; ++i) {
    original_sum += 2 * i;
    CHECK(check_preserved_registers(checked_resume, &context, 0x1122334455667788));
    CHECK(x87_rounding() == x87 && _mm_getcsr() == mxcsr);
    CHECK(original_sum == 17 + i * (i + 1));
    CHECK(run.alternate_sum == 29 + 3 * i * (i + 1) / 2);
    CHECK(worker_marker == 2);
    worker_marker = 1;
  }
  CHECK(!context.resume());
  CHECK(x87_rounding() == x87 && _mm_getcsr() == mxcsr);
  CHECK(std::fesetenv(&original_fp) == 0);
  std::printf("PASS %llu round trips, original=%llu alternate=%llu\n",
              static_cast<unsigned long long>(count),
              static_cast<unsigned long long>(original_sum),
              static_cast<unsigned long long>(run.alternate_sum));
}

template <typename Exception, typename Action>
void expect_exception(Action action)
{
  try {
    action();
  } catch (const Exception &) {
    return;
  }
  CHECK(false);
}

void entry_must_not_run(WorkerContext &, void *)
{
  CHECK(false);
}

void run_lifecycle_checks()
{
  expect_exception<std::invalid_argument>([] { WorkerContext context(nullptr, nullptr); });
  WorkerContext idle(entry_must_not_run, nullptr);
  expect_exception<std::logic_error>([&] { idle.yield(); });
  std::thread foreign([&] {
    expect_exception<std::logic_error>([&] { idle.resume(); });
    expect_exception<std::logic_error>([&] { idle.yield(); });
  });
  foreign.join();

  WorkerContext normal([](WorkerContext &context, void *arg) {
    auto &idle = *static_cast<WorkerContext *>(arg);
    expect_exception<std::logic_error>([&] { context.resume(); });
    expect_exception<std::logic_error>([&] { idle.resume(); });
  }, &idle);
  CHECK(!normal.resume());
  expect_exception<std::logic_error>([&] { normal.resume(); });

  bool unwound = false;
  WorkerContext failing([](WorkerContext &context, void *arg) {
    struct Guard {
      bool &unwound;
      ~Guard() { unwound = true; }
    } guard{*static_cast<bool *>(arg)};
    context.yield();
    throw std::runtime_error("entry failed");
  }, &unwound);
  CHECK(failing.resume());
  CHECK(!unwound);
  expect_exception<std::runtime_error>([&] { failing.resume(); });
  CHECK(unwound);
  expect_exception<std::logic_error>([&] { failing.resume(); });
  std::puts("PASS lifecycle, thread ownership, invalid transitions and exception cleanup");
}

bool usr1_blocked()
{
  sigset_t mask;
  CHECK(pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0);
  return sigismember(&mask, SIGUSR1) == 1;
}

void run_signal_mask_check()
{
  sigset_t signal, saved;
  CHECK(sigemptyset(&signal) == 0);
  CHECK(sigaddset(&signal, SIGUSR1) == 0);
  CHECK(pthread_sigmask(SIG_UNBLOCK, &signal, &saved) == 0);
  WorkerContext context([](WorkerContext &context, void *arg) {
    CHECK(!usr1_blocked());
    CHECK(pthread_sigmask(SIG_BLOCK, static_cast<sigset_t *>(arg), nullptr) == 0);
    context.yield();
    CHECK(usr1_blocked());
  }, &signal);
  CHECK(context.resume());
  CHECK(!usr1_blocked());
  CHECK(!context.resume());
  CHECK(!usr1_blocked());
  CHECK(pthread_sigmask(SIG_SETMASK, &saved, nullptr) == 0);
  std::puts("PASS glibc signal-mask save/restore");
}

} // namespace

extern "C" void context_test_entry(WorkerContext &context, void *argument)
{
  auto &run = *static_cast<RoundTrips *>(argument);
  volatile std::uint64_t alternate_sum = 29;
  CHECK(!on_pthread_stack(run, &alternate_sum));
  CHECK(std::this_thread::get_id() == run.owner);
  CHECK(x87_rounding() == FE_DOWNWARD);
  CHECK(std::fesetround(FE_UPWARD) == 0);
  const unsigned x87 = x87_rounding();
  const unsigned mxcsr = _mm_getcsr();
  for (std::uint64_t i = 1; i <= run.count; ++i) {
    CHECK(worker_marker == 1);
    worker_marker = 2;
    alternate_sum += 3 * i;
    run.alternate_sum = alternate_sum;
    CHECK(check_preserved_registers(checked_yield, &context, 0x8877665544332211));
    CHECK(x87_rounding() == x87 && _mm_getcsr() == mxcsr);
    CHECK(alternate_sum == 29 + 3 * i * (i + 1) / 2);
  }
}

int main()
{
  run_lifecycle_checks();
  run_signal_mask_check();
  run_round_trips(1);
  run_round_trips(10000);
  run_round_trips(1000000);
}
