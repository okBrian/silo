#include "signal_backend.h"
#include <cerrno>
#include <climits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" uint64_t silo_signal_window(const uint64_t *, unsigned);
extern "C" char silo_signal_begin[], silo_signal_end[];
extern "C" char silo_signal_loop_end[];
extern "C" [[noreturn]] void silo_signal_return(void *);

// SysV leaf: keep all C++ and libc outside the capture interval. The syscall
// mask contains SIGUSR2 only. Sum and XMM0 canaries detect continuation damage.
asm(R"(
.text
.globl silo_signal_window
.type silo_signal_window,@function
silo_signal_window:
 push %r12
 push %r13
 push %r14
 mov %rdi,%r13
 mov %esi,%r14d
 sub $1536,%rsp
 mov %rsp,%rdi
 xor %eax,%eax
 mov $192,%ecx
 rep stosq
 fxsave64 (%rsp)
 fninit
 fld1
 fldpi
 .irp i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
 pcmpeqd %xmm\i,%xmm\i
 .endr
 fxsave64 512(%rsp)
 xor %r12d,%r12d
 mov %r13,%rsi
 mov $1,%edi
 xor %edx,%edx
 mov $8,%r10d
 mov $14,%eax
 syscall
.globl silo_signal_begin
silo_signal_begin:
 nop
2:
 add %r14,%r12
 dec %r14d
 jnz 2b
.globl silo_signal_loop_end
silo_signal_loop_end:
 mov %r13,%rsi
 xor %edi,%edi
 xor %edx,%edx
 mov $8,%r10d
 mov $14,%eax
 syscall
.globl silo_signal_end
silo_signal_end:
 fxsave64 1024(%rsp)
 lea 512(%rsp),%rsi
 lea 1024(%rsp),%rdi
 mov $416,%ecx
 repe cmpsb
 je 1f
 xor %r12d,%r12d
1:
 fxrstor64 (%rsp)
 add $1536,%rsp
 mov %r12,%rax
 pop %r14
 pop %r13
 pop %r12
 ret
.size silo_signal_window,.-silo_signal_window
.globl silo_signal_return
.type silo_signal_return,@function
silo_signal_return:
 mov %rdi,%rsp
 mov $15,%eax
 syscall
 ud2
.size silo_signal_return,.-silo_signal_return
.section .note.GNU-stack,"",@progbits
.text
)");

thread_local SignalBackend *SignalBackend::active_ = nullptr;
namespace {
std::mutex installation_mutex;
unsigned users = 0;
struct sigaction previous_action;
void checked(int status, const char *what) {
  if (status) throw std::system_error(status, std::generic_category(), what);
}
void increment(volatile sig_atomic_t &n) { if (n < INT_MAX) ++n; }
}

SignalBackend::SignalBackend() {
  if (active_) throw std::logic_error("nested signal backend");
  sigset_t all;
  sigfillset(&all);
  checked(pthread_sigmask(SIG_SETMASK, &all, &previous_mask_), "block worker signals");
  try {
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    // Two independently guarded stacks: retained kernel frame and C landing.
    const size_t usable = 1024 * 1024;
    mapping_size_ = 2 * (page + usable);
    mapping_ = mmap(nullptr, mapping_size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping_ == MAP_FAILED) { mapping_ = nullptr; throw std::runtime_error("signal stack mmap"); }
    char *base = static_cast<char *>(mapping_);
    if (mprotect(base + page, usable, PROT_READ | PROT_WRITE) ||
        mprotect(base + 2 * page + usable, usable, PROT_READ | PROT_WRITE))
      throw std::runtime_error("signal stack mprotect");
    stack_t stack{};
    stack.ss_sp = base + page;
    stack.ss_size = usable;
    if (sigaltstack(&stack, &previous_stack_)) throw std::runtime_error("sigaltstack");
    landing_top_ = reinterpret_cast<uintptr_t>(base + mapping_size_) - 8;
    *reinterpret_cast<uintptr_t *>(landing_top_) = 0; // SysV function entry alignment.
    {
      std::lock_guard<std::mutex> lock(installation_mutex);
      if (!users) {
        if (sigaction(SIGUSR2, nullptr, &previous_action) ||
            (previous_action.sa_handler != SIG_DFL && previous_action.sa_handler != SIG_IGN)) {
          sigaltstack(&previous_stack_, nullptr);
          throw std::runtime_error("SIGUSR2 is already owned");
        }
        struct sigaction action{};
        action.sa_sigaction = handler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigfillset(&action.sa_mask);
        if (sigaction(SIGUSR2, &action, &previous_action)) {
          sigaltstack(&previous_stack_, nullptr);
          throw std::runtime_error("signal backend sigaction");
        }
      }
      ++users;
    }
    thread_ = pthread_self();
    active_ = this;
  } catch (...) {
    if (mapping_) munmap(mapping_, mapping_size_);
    pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    throw;
  }
}

SignalBackend::~SignalBackend() {
  // Sender registration must already be removed. Consume stale, coalesced
  // thread-directed notifications before restoring the previous disposition.
  sigset_t set;
  sigemptyset(&set); sigaddset(&set, SIGUSR2);
  timespec zero{};
  while (sigtimedwait(&set, nullptr, &zero) >= 0) {}
  active_ = nullptr;
  if (sigaltstack(&previous_stack_, nullptr)) std::terminate();
  {
    std::lock_guard<std::mutex> lock(installation_mutex);
    if (!--users && sigaction(SIGUSR2, &previous_action, nullptr)) std::terminate();
  }
  munmap(mapping_, mapping_size_);
  if (pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr)) std::terminate();
}

void SignalBackend::send() {
  checked(pthread_kill(thread_, SIGUSR2), "pthread_kill SIGUSR2");
  ++sends_;
}

void SignalBackend::window(WorkerContext &context, bool eligible, unsigned rounds) {
  if (!rounds) throw std::invalid_argument("zero signal window");
  context_ = &context;
  eligible_ = eligible;
  if (eligible && pending_) {
    checked(pthread_kill(thread_, SIGUSR2), "rearm deferred signal");
    ++rearms_;
  }
  const uint64_t mask = UINT64_C(1) << (SIGUSR2 - 1);
  const uint64_t result = silo_signal_window(&mask, rounds);
  continuation_ok_ &= result == uint64_t(rounds) * (uint64_t(rounds) + 1) / 2;
  eligible_ = 0;
}

void SignalBackend::handler(int, siginfo_t *, void *raw) {
  SignalBackend *self = active_;
  if (!self) return;
  increment(self->handlers_);
  auto *frame = static_cast<ucontext_t *>(raw);
  const auto rip = static_cast<uintptr_t>(frame->uc_mcontext.gregs[REG_RIP]);
  if (!self->eligible_ || rip < reinterpret_cast<uintptr_t>(silo_signal_begin) ||
      rip >= reinterpret_cast<uintptr_t>(silo_signal_end)) {
    self->pending_ = 1;
    increment(self->deferred_);
    return;
  }
  increment(self->captures_);
  self->pending_ = 0;
  if (rip > reinterpret_cast<uintptr_t>(silo_signal_begin) &&
      rip < reinterpret_cast<uintptr_t>(silo_signal_loop_end)) increment(self->interior_);
  self->eligible_ = 0; // No second capture of this retained frame/landing stack.
  self->frame_ = frame;
  self->rip_ = frame->uc_mcontext.gregs[REG_RIP];
  self->rsp_ = frame->uc_mcontext.gregs[REG_RSP];
  self->rdi_ = frame->uc_mcontext.gregs[REG_RDI];
  self->frame_mask_ = frame->uc_sigmask;
  // Plain fixed-size assignments only; no allocator, lock, logger or context API.
  sigfillset(&frame->uc_sigmask);
  frame->uc_mcontext.gregs[REG_RIP] = reinterpret_cast<greg_t>(&landing);
  frame->uc_mcontext.gregs[REG_RSP] = self->landing_top_;
  frame->uc_mcontext.gregs[REG_RDI] = reinterpret_cast<greg_t>(self);
}

void SignalBackend::landing(SignalBackend *self) noexcept {
  // The handler has RETURNED through the kernel. This runs on the landing
  // stack, with all signals blocked, while the original frame stays untouched.
  self->context_->yield();
  auto *frame = self->frame_;
  frame->uc_mcontext.gregs[REG_RIP] = self->rip_;
  frame->uc_mcontext.gregs[REG_RSP] = self->rsp_;
  frame->uc_mcontext.gregs[REG_RDI] = self->rdi_;
  frame->uc_sigmask = self->frame_mask_;
  if (self->corrupt_) {
    const unsigned mask = self->corrupt_;
    self->corrupt_ = 0;
    if (mask & 1) frame->uc_mcontext.gregs[REG_R12] ^= 1;
    if (mask & 2) frame->uc_mcontext.fpregs->_xmm[0].element[0] ^= 1;
  }
  // x86-64 rt_sigreturn expects RSP at the ucontext, after the restorer slot.
  // Reuse the original complete frame, including the kernel-sized XSAVE area.
  silo_signal_return(frame);
}
