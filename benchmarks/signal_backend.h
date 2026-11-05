#pragma once
#include "worker_context.h"
#include <signal.h>
#include <cstdint>
#include <atomic>

// Experimental Linux/x86-64 transport. Only window()'s assembly is eligible
// for capture. Every other instruction executes with signals blocked. The
// original kernel frame remains on a dedicated altstack until final sigreturn.
class SignalBackend {
public:
  SignalBackend();
  ~SignalBackend();
  SignalBackend(const SignalBackend &) = delete;
  SignalBackend &operator=(const SignalBackend &) = delete;
  void window(WorkerContext &context, bool eligible, unsigned rounds = 256);
  void send(); // Ordinary sender thread; caller serializes against destruction.
  unsigned handlers() const { return handlers_; }
  unsigned captures() const { return captures_; }
  unsigned deferred() const { return deferred_; }
  unsigned interior_captures() const { return interior_; }
  unsigned sends() const { return sends_.load(); }
  unsigned rearms() const { return rearms_; }
  void corrupt_next_capture(unsigned mask = 3) { corrupt_ = mask; } // GPR=1, SIMD=2; tests only.
  bool continuation_ok() const { return continuation_ok_; }
private:
  static void handler(int, siginfo_t *, void *);
  static void landing(SignalBackend *) noexcept;
  static thread_local SignalBackend *active_;
  sigset_t previous_mask_{};
  stack_t previous_stack_{};
  void *mapping_ = nullptr;
  size_t mapping_size_ = 0;
  uintptr_t landing_top_ = 0;
  unsigned long thread_ = 0;
  WorkerContext *context_ = nullptr;
  ucontext_t *frame_ = nullptr;
  greg_t rip_ = 0, rsp_ = 0, rdi_ = 0;
  sigset_t frame_mask_{};
  volatile sig_atomic_t eligible_ = 0, handlers_ = 0, captures_ = 0, deferred_ = 0, corrupt_ = 0, interior_ = 0, pending_ = 0;
  std::atomic<unsigned> sends_{0};
  unsigned rearms_ = 0;
  bool continuation_ok_ = true;
};
