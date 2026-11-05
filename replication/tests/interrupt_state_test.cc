#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <ucontext.h>

struct Snapshot {
  uint64_t gpr[16];
  uint64_t flags;
  uint64_t red_zone;
  alignas(16) unsigned char fxsave[512];
};

static_assert(offsetof(Snapshot, fxsave) == 144, "assembly snapshot layout");

extern "C" {
Snapshot interrupt_state_before, interrupt_state_after;
void interrupt_state_round();
void clobber_interrupt_state();
}

namespace {

volatile sig_atomic_t hits = 0;
volatile sig_atomic_t corrupt_frame = 0;

void trap_handler(int, siginfo_t *, void *frame)
{
  ++hits;
  if (corrupt_frame) {
    auto &state = static_cast<ucontext_t *>(frame)->uc_mcontext;
    state.gregs[REG_RAX] ^= 1;
    state.fpregs->_xmm[0].element[0] ^= 1;
  }
  clobber_interrupt_state();
}

void check(bool condition, const char *message)
{
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

} // namespace

int main()
{
  struct sigaction action = {};
  action.sa_sigaction = trap_handler;
  action.sa_flags = SA_SIGINFO;
  check(sigemptyset(&action.sa_mask) == 0 && sigaction(SIGTRAP, &action, nullptr) == 0,
        "cannot install SIGTRAP handler");

  const auto &before = interrupt_state_before;
  const auto &after = interrupt_state_after;
  // FXSAVE bytes 0..415 contain legacy FP, MXCSR and all sixteen XMM registers.
  constexpr size_t used_fxsave = 416;
  constexpr unsigned rounds = 256;
  for (unsigned i = 0; i < rounds; ++i) {
    interrupt_state_round();
    check(std::memcmp(before.gpr, after.gpr, sizeof(before.gpr)) == 0, "GPR mismatch");
    check(before.flags == after.flags, "RFLAGS mismatch");
    check(before.red_zone == after.red_zone, "stack canary mismatch");
    check(std::memcmp(before.fxsave, after.fxsave, used_fxsave) == 0, "FP/SSE mismatch");
  }
  check(hits == rounds, "SIGTRAP delivery count mismatch");
  std::printf("PASS %u Linux trap returns: GPRs/RSP, RFLAGS, red zone, x87/MXCSR/XMM0-15\n",
              rounds);

  corrupt_frame = 1;
  interrupt_state_round();
  check(hits == rounds + 1 && (before.gpr[0] ^ after.gpr[0]) == 1 &&
        (before.fxsave[160] ^ after.fxsave[160]) == 1,
        "negative control did not expose corrupted RAX and XMM0");
  std::puts("PASS negative control: saved RAX and XMM0 corruption detected");
}
