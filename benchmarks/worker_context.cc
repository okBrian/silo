#include "worker_context.h"
#include "../preemption.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>
#include <unistd.h>

thread_local WorkerContext *WorkerContext::active_ = nullptr;

WorkerContext::WorkerContext(Entry entry, void *argument)
    : owner_(std::this_thread::get_id()), entry_(entry), argument_(argument)
{
  if (!entry_)
    throw std::invalid_argument("worker context requires an entry function");
  if (getcontext(&alternate_) == -1)
    throw std::system_error(errno, std::generic_category(), "getcontext");

  constexpr std::size_t stack_size = 1024 * 1024;
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0)
    throw std::runtime_error("cannot determine worker stack guard size");
  mapping_size_ = stack_size + page_size;
  stack_ = mmap(nullptr, mapping_size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (stack_ == MAP_FAILED)
    throw std::system_error(errno, std::generic_category(), "mmap worker stack");

  void *usable_stack = static_cast<char *>(stack_) + page_size;
  if (mprotect(usable_stack, stack_size, PROT_READ | PROT_WRITE) == -1) {
    const int error = errno;
    munmap(stack_, mapping_size_);
    throw std::system_error(error, std::generic_category(), "mprotect worker stack");
  }

  alternate_.uc_stack.ss_sp = usable_stack;
  alternate_.uc_stack.ss_size = stack_size;
  alternate_.uc_stack.ss_flags = 0;
  // glibc returns to the most recent resume() when start() returns.
  alternate_.uc_link = &original_;
  makecontext(&alternate_, &WorkerContext::start, 0);
}

WorkerContext::~WorkerContext()
{
  // Suspended frames may still own resources. Require the entry to unwind
  // normally before releasing its stack.
  if (std::this_thread::get_id() != owner_ ||
      state_ == State::running || state_ == State::suspended)
    std::terminate();
  munmap(stack_, mapping_size_);
}

bool
WorkerContext::resume()
{
  if (std::this_thread::get_id() != owner_ || active_ ||
      ScopedNonPreemptible::blocked() ||
      (state_ != State::ready && state_ != State::suspended))
    throw std::logic_error("invalid worker context resume");
  active_ = this;
  state_ = State::running;
  if (swapcontext(&original_, &alternate_) == -1)
    std::terminate();
  active_ = nullptr;
  if (failure_)
    std::rethrow_exception(failure_);
  return state_ == State::suspended;
}

void
WorkerContext::request_service()
{
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("worker notification from a different thread");
  service_pending_ = true;
}

bool
WorkerContext::checkpoint()
{
  if (active_ != this)
    throw std::logic_error("invalid worker context checkpoint");
  if (!service_pending_ || ScopedNonPreemptible::blocked())
    return false;
  service_pending_ = false;
  yield();
  return true;
}

void
WorkerContext::yield()
{
  if (active_ != this || ScopedNonPreemptible::blocked())
    throw std::logic_error("invalid worker context yield");
  state_ = State::suspended;
  if (swapcontext(&alternate_, &original_) == -1)
    std::terminate();
}

void
WorkerContext::start() noexcept
{
  WorkerContext &context = *active_;
  try {
    context.entry_(context, context.argument_);
  } catch (...) {
    context.failure_ = std::current_exception();
  }
  // Returning through uc_link also transfers control to the original stack.
  if (ScopedNonPreemptible::blocked())
    std::terminate();
  context.state_ = State::finished;
}
