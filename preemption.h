#pragma once

// Worker-owned nesting across both stacks. A guard never switches stacks on
// exit: the next explicit checkpoint runs after cleanup and outside unwinding.
class ScopedNonPreemptible {
public:
  explicit ScopedNonPreemptible(bool protect = true) noexcept
    : protected_(protect)
  {
    if (protected_)
      ++depth();
  }

  ~ScopedNonPreemptible()
  {
    if (protected_)
      --depth();
  }

  ScopedNonPreemptible(const ScopedNonPreemptible &) = delete;
  ScopedNonPreemptible &operator=(const ScopedNonPreemptible &) = delete;

  static bool blocked() noexcept { return depth() != 0; }

private:
  static unsigned &depth() noexcept
  {
    static thread_local unsigned value = 0;
    return value;
  }

  const bool protected_;
};

// Compile the engine instrumentation out of the stock benchmark build.
#ifdef SILO_WORKER_CONTEXTS
using SiloOperationRegion = ScopedNonPreemptible;
#else
class SiloOperationRegion {
public:
  explicit SiloOperationRegion(bool = true) noexcept {}
};
#endif
