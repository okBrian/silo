#pragma once

#include "abstract_db.h"
#include "../util.h"

// One instance per logical execution context, at a stable address while an
// attempt is live. Retry state lives with the request and its executing stack.
class bench_txn_context {
public:
  bench_txn_context(unsigned long seed, const abstract_db &db, uint64_t flags)
    : r(seed)
  {
    txn_obj_buf.reserve(str_arena::MinStrReserveLength);
    txn_obj_buf.resize(db.sizeof_txn_object(flags));
  }

  void *txn_buf() { return &txn_obj_buf[0]; }
  std::string &str() { return *arena.next(); }

  util::fast_random r;
  str_arena arena;

private:
  std::string txn_obj_buf;
};
