#ifndef SILO_WORKER_CONTEXT_H
#define SILO_WORKER_CONTEXT_H

#include <features.h>
#if !defined(__linux__) || !defined(__x86_64__) || !defined(__LP64__) || !defined(__GLIBC__)
#error "WorkerContext requires x86-64 Linux with glibc"
#endif

#include <cstddef>
#include <exception>
#include <thread>
#include <ucontext.h>

// One worker's original pthread stack and one owned, guarded stack.
// Use on the constructing thread. Switch only from ordinary code, outside
// exception handlers and unwinding. TLS is shared. Nested pairs are unsupported.
class WorkerContext {
public:
  using Entry = void (*)(WorkerContext &, void *);

  WorkerContext(Entry entry, void *argument);
  // Destroy on the original stack before the first resume or after completion.
  ~WorkerContext();

  WorkerContext(const WorkerContext &) = delete;
  WorkerContext &operator=(const WorkerContext &) = delete;

  // Call from the original stack. Returns true on yield and false on normal
  // completion. An exception escaping the entry is rethrown on this stack.
  // A completed context cannot be resumed.
  bool resume();

  // Coalesces notifications on the owning thread. The original stack services
  // the queue before resuming. A notification during service can rearm it.
  void request_service();

  // Call from the entry stack at an audited, voluntary checkpoint. Returns
  // true after service and resume. Protected checkpoints leave work pending.
  bool checkpoint();

  // Call from the entry stack. Returns when the original stack resumes it.
  // Like resume(), this rejects a transition while a protected region is live.
  void yield();

private:
  enum class State { ready, running, suspended, finished };

  static void start() noexcept;
  static thread_local WorkerContext *active_;

  const std::thread::id owner_;
  const Entry entry_;
  void *const argument_;
  ucontext_t original_{};
  ucontext_t alternate_{};
  void *stack_;
  std::size_t mapping_size_;
  State state_ = State::ready;
  std::exception_ptr failure_;
  bool service_pending_ = false;
};

#endif
