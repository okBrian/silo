// Exercise the exact benchmark adapters and their private TPC-C fixtures.
#include "../../benchmarks/tpcc.cc"
#include "../../benchmarks/ndb_wrapper_impl.h"
#ifdef SILO_WORKER_CONTEXTS
#include "../../benchmarks/worker_context.h"
#include "batched_notification_cases.h"
#include "starvation_cases.h"
#endif

namespace {

class TestDb : public ndb_wrapper<transaction_proto2> {
public:
  TestDb() : ndb_wrapper({}, {}, false, false, false) {}

  bool fail_commit = false;

  bool commit_txn(void *txn) override
  {
    if (fail_commit) {
      fail_commit = false;
      return false; // The caller must abort the still-live Silo transaction.
    }
    return ndb_wrapper::commit_txn(txn);
  }
};

class TestWorker : public tpcc_worker {
public:
  using tpcc_worker::tpcc_worker;

  queued::Driver::Prepared prepare(const queued::Request &request)
  {
    return prepare_queued_request(request, *this);
  }
};

template<typename T>
T blank_value()
{
  T value;
  std::fill_n(reinterpret_cast<char *>(&value), sizeof(value), 0);
  return value;
}

struct Fixture {
  TestDb db;
  bench_txn_context state{1, db, 0};
  map<string, abstract_ordered_index *> tables;
  map<string, vector<abstract_ordered_index *>> partitions;
  bool probe_signal_abort = false;
  bool signal_low_live = false;
  unsigned signal_abort_probes = 0;

  Fixture()
  {
#define OPEN_TEST_TABLE(name) \
    tables[#name] = db.open_index(#name, sizeof(name::value), false); \
    partitions[#name] = {tables.at(#name), tables.at(#name)};
    TPCC_TABLE_LIST(OPEN_TEST_TABLE)
#undef OPEN_TEST_TABLE
    db.thread_init(false);
  }

  ~Fixture()
  {
    db.thread_end();
    for (const auto &entry : tables)
      db.close_index(entry.second);
  }

  template<typename Key, typename Value>
  void write(const char *table, const Key &key, const Value &value)
  {
    if (probe_signal_abort && signal_low_live && std::string(table) == "stock") {
      // A real failed Silo write/abort on the high context, while the original
      // Q2 attempt remains suspended. This is an extra engine cleanup probe,
      // separate from the driver's scripted conflict/attempt counters.
      Value before, after;
      ALWAYS_ASSERT(read(table, key, before));
      {
        scoped_str_arena scope(state.arena);
        void *txn = db.new_txn(0, state.arena, state.txn_buf(), abstract_db::HINT_DEFAULT);
        tables.at(table)->put(txn, Encode(state.str(), key), Encode(state.str(), value));
        db.fail_commit = true;
        ALWAYS_ASSERT(!db.commit_txn(txn));
        db.abort_txn(txn);
      }
      ALWAYS_ASSERT(!ScopedNonPreemptible::blocked() && read(table, key, after));
      std::string before_encoded, after_encoded;
      Encode(before_encoded, before);
      Encode(after_encoded, after);
      ALWAYS_ASSERT(before_encoded == after_encoded);
      ++signal_abort_probes;
    }
    scoped_str_arena scope(state.arena);
    void *txn = db.new_txn(0, state.arena, state.txn_buf(), abstract_db::HINT_DEFAULT);
    tables.at(table)->put(txn, Encode(state.str(), key), Encode(state.str(), value));
    ALWAYS_ASSERT(db.commit_txn(txn));
  }

  template<typename Value, typename Key>
  bool read(const char *table, const Key &key, Value &value)
  {
    scoped_str_arena scope(state.arena);
    void *txn = db.new_txn(0, state.arena, state.txn_buf(), abstract_db::HINT_DEFAULT);
    std::string raw;
    const bool found = tables.at(table)->get(txn, Encode(state.str(), key), raw);
    if (found)
      Decode(raw, value);
    ALWAYS_ASSERT(db.commit_txn(txn));
    return found;
  }

#ifdef SILO_WORKER_CONTEXTS
  bool run_signal_low(tpcc_q2::Tables &q2, uint64_t &result,
                      const std::function<void()> &checkpoint) {
    bench_txn_context low(1, db, transaction_base::TXN_FLAG_READ_ONLY);
    struct Live {
      bool &flag;
      explicit Live(bool &value) : flag(value) { flag = true; }
      ~Live() { flag = false; }
    } live(signal_low_live);
    return q2.run(&db, low, 0, result, checkpoint);
  }
#endif

  void q2()
  {
    tpcc_q2::Tables q2(&db, partitions.at("stock"), tables.at("item"), 6, 9);
    q2.register_indexes(tables);
    for (unsigned r = 0; r < 3; ++r)
      write("q2_region", q2_dimension::key(r), q2_dimension::value(r));
    write("q2_nation", q2_dimension::key(48), q2_dimension::value(0));
    write("q2_nation", q2_dimension::key(49), q2_dimension::value(1));
    // Supplier 0 has no mapped stock. Supplier 8 has no matching nation.
    const unsigned nation_by_supplier[] = {48, 99, 48, 49, 48, 99, 48, 99, 99};
    for (unsigned s = 0; s < ARRAY_NELEMS(nation_by_supplier); ++s)
      write("q2_supplier", q2_dimension::key(s), q2_dimension::value(nation_by_supplier[s]));

    for (unsigned i = 1; i <= 6; ++i) {
      auto value = blank_value<item::value>();
      value.i_data.assign(i == 4 ? "bad" : "GOODB");
      write("item", item::key(i), value);
    }
    write("stock", stock::key(1, 2), stock::value(4, 0, 0, 0));
    write("stock", stock::key(2, 1), stock::value(9, 0, 0, 0));
    write("stock", stock::key(1, 3), stock::value(9, 0, 0, 0));
    write("stock", stock::key(1, 4), stock::value(3, 0, 0, 0));
    write("stock", stock::key(2, 2), stock::value(8, 0, 0, 0));
    write("stock", stock::key(1, 6), stock::value(7, 0, 0, 0));
    write("stock", stock::key(2, 3), stock::value(7, 0, 0, 0));
    write("stock", stock::key(2, 4), stock::value(1, 0, 0, 0));
    db.do_txn_epoch_sync();

    // Independently computed from explicit expected rows as packed LE uint32s.
    // Region 0: (0,48,2,1,2,4), (0,48,6,1,6,7).
    // Region 1: (1,49,3,1,3,9). Region 2: no rows.
    const uint64_t expected[] = {UINT64_C(12860241382490130518), UINT64_C(11649938069444573197),
                                 UINT64_C(14695981039346656037)};
    uint64_t checksum = 0;
    db.fail_commit = true;
    ALWAYS_ASSERT(!q2.run(&db, state, 0, checksum) && checksum == 0);
    for (unsigned r = 0; r < ARRAY_NELEMS(expected); ++r) {
      ALWAYS_ASSERT(q2.run(&db, state, r, checksum) && checksum == expected[r]);
      std::cout << "PASS Q2 region " << r << " checksum " << checksum << '\n';
    }
#ifdef SILO_WORKER_CONTEXTS
    struct PausedQ2 {
      Fixture &fixture;
      const tpcc_q2::Tables &tables;
      uint64_t checksum = 0;
      unsigned reads = 0;

      PausedQ2(Fixture &fixture, const tpcc_q2::Tables &tables)
        : fixture(fixture), tables(tables) {}
    } paused(*this, q2);
    WorkerContext context([](WorkerContext &context, void *arg) {
      auto &test = *static_cast<PausedQ2 *>(arg);
      bench_txn_context low(1, test.fixture.db, transaction_base::TXN_FLAG_READ_ONLY);
      ALWAYS_ASSERT(test.tables.run(&test.fixture.db, low, 0, test.checksum, [&] {
        ALWAYS_ASSERT(!ScopedNonPreemptible::blocked());
        if (++test.reads % 2 == 0)
          context.yield();
      }));
    }, &paused);
    unsigned yields = 0;
    while (context.resume()) {
      ++yields;
      // Execute a write transaction using independent storage while the
      // snapshot owns its original stack, buffers and epoch guard.
      write("stock", stock::key(1, 2), stock::value(40, 0, 0, 0));
    }
    ALWAYS_ASSERT(paused.reads == 18 && yields == 9 && paused.checksum == expected[0]);
    std::cout << "PASS Q2 checkpoint count and snapshot checksum across nine write handoffs\n";
    probe_signal_abort = notification_cases::signal_backend();
    // Same controlled transition matrix as the standalone driver, now with a
    // real Silo snapshot and intervening committed stock writes. Recompute the
    // unpaused oracle before each case because previous cases update stock.
    for (auto point : {queued::ServiceTransition::high_entry,
                       queued::ServiceTransition::high_empty,
                       queued::ServiceTransition::before_low_resume}) {
      for (unsigned mode = 0; mode < 3; ++mode) {
        uint64_t oracle = 0;
        ALWAYS_ASSERT(q2.run(&db, state, 0, oracle));
        notification_cases::transition_case(point, mode,
          [&](uint64_t &result, const std::function<void()> &checkpoint) {
            return run_signal_low(q2, result, checkpoint);
          }, [&](const queued::Request &request) {
            write("stock", stock::key(1, 2), stock::value(20 + request.high_sequence, 0, 0, 0));
          }, oracle, "silo-q2");
      }
    }
    std::cout << "PASS nine Silo Q2 notification transition cases with committed writes\n";
    for (unsigned threshold : {0u, 100u}) {
      uint64_t oracle = 0;
      ALWAYS_ASSERT(q2.run(&db, state, 0, oracle));
      starvation_cases::limits(threshold,
        [&](uint64_t &result, const std::function<void()> &checkpoint) {
          return run_signal_low(q2, result, checkpoint);
        }, [&](const queued::Request &request) {
          write("stock", stock::key(1, 2), stock::value(20 + request.high_sequence, 0, 0, 0));
        }, oracle, "silo-q2-limits");
    }
    for (bool expire : {false, true}) {
      uint64_t oracle = 0;
      ALWAYS_ASSERT(q2.run(&db, state, 0, oracle));
      starvation_cases::controlled(
        [&](uint64_t &result, const std::function<void()> &checkpoint) {
          return run_signal_low(q2, result, checkpoint);
        }, [&](const queued::Request &request) {
          write("stock", stock::key(1, 2), stock::value(20 + request.high_sequence, 0, 0, 0));
        }, oracle, expire, "silo-q2-starvation");
    }
    std::cout << "PASS Silo Q2 starvation equality, recovery, retry, reset and drain cases\n";
    if (probe_signal_abort) {
      ALWAYS_ASSERT(signal_abort_probes == 53);
      std::cout << "PASS 53 actual Silo failed-write rollback/cleanup probes while Q2 remains suspended in signal backend\n";
      probe_signal_abort = false;
    }

#endif
  }

  void short_transactions()
  {
    TestWorker worker(0, 7, &db, tables, partitions, nullptr, nullptr, 1, 2, nullptr);
    queued::Request request;
    request.kind = queued::Kind::new_order;
    request.home_warehouse = 1;
    request.high_sequence = 7;
    constexpr uint32_t date = (UINT32_C(1) << 31) + 7;

    g_new_order_remote_item_pct = 100;
    util::fast_random rng(0);
    NewOrderInput order;
    do {
      rng.set_seed(++request.input_seed);
      order = make_new_order_input(rng, 1, date);
    } while (std::set<uint>(order.item_ids, order.item_ids + order.num_items).size() != order.num_items);
    ALWAYS_ASSERT(!order.all_local);

    auto warehouse_row = blank_value<warehouse::value>();
    warehouse_row.w_zip.assign("123456789");
    write("warehouse", warehouse::key(1), warehouse_row);
    auto district_row = blank_value<district::value>();
    district_row.d_zip.assign("123456789");
    district_row.d_next_o_id = 3001;
    const district::key district_key(1, order.district);
    write("district", district_key, district_row);
    auto customer_row = blank_value<customer::value>();
    customer_row.c_credit.assign("GC");
    customer_row.c_middle.assign("OE");
    write("customer", customer::key(1, order.district, order.customer), customer_row);
    for (uint i = 0; i < order.num_items; ++i) {
      ALWAYS_ASSERT(order.supplier_warehouses[i] == 2);
      auto item_row = blank_value<item::value>();
      item_row.i_price = 2;
      write("item", item::key(order.item_ids[i]), item_row);
      write("stock", stock::key(2, order.item_ids[i]), stock::value(10, 0, 0, 0));
    }

    auto attempt = worker.prepare(request);
    uint64_t checksum = 0;
    db.fail_commit = true;
    ALWAYS_ASSERT(attempt(checksum) == queued::Attempt::conflict);
    const oorder::key order_key(1, order.district, 3001);
    oorder::value order_row;
    ALWAYS_ASSERT(!read("oorder", order_key, order_row));
    ALWAYS_ASSERT(read("district", district_key, district_row) && district_row.d_next_o_id == 3001);
    ++request.input_seed;
    ++request.high_sequence;
    ALWAYS_ASSERT(attempt(checksum) == queued::Attempt::committed);
    ALWAYS_ASSERT(read("district", district_key, district_row) && district_row.d_next_o_id == 3002);
    ALWAYS_ASSERT(read("oorder", order_key, order_row));
    ALWAYS_ASSERT(order_row.o_c_id == int(order.customer) && order_row.o_entry_d == date);
    ALWAYS_ASSERT(order_row.o_ol_cnt == int(order.num_items) && !order_row.o_all_local);
    new_order::value new_order_row;
    ALWAYS_ASSERT(read("new_order", new_order::key(1, order.district, 3001), new_order_row));
    oorder_c_id_idx::value customer_index;
    ALWAYS_ASSERT(read("oorder_c_id_idx", oorder_c_id_idx::key(1, order.district, order.customer, 3001),
                       customer_index));
    for (uint i = 0; i < order.num_items; ++i) {
      const int qty = order.quantities[i];
      order_line::value line;
      ALWAYS_ASSERT(read("order_line", order_line::key(1, order.district, 3001, i + 1), line));
      ALWAYS_ASSERT(line.ol_i_id == int(order.item_ids[i]) && line.ol_supply_w_id == 2);
      ALWAYS_ASSERT(line.ol_quantity == qty && line.ol_amount == 2 * qty && !line.ol_delivery_d);
      stock::value stock_row;
      ALWAYS_ASSERT(read("stock", stock::key(2, order.item_ids[i]), stock_row));
      ALWAYS_ASSERT(stock_row.s_quantity == 101 - qty && stock_row.s_ytd == qty && stock_row.s_remote_cnt == 1);
    }
    std::cout << "PASS queued New-Order rollback, retry, linked rows and remote stock changes\n";

    request.kind = queued::Kind::payment;
    PaymentInput payment;
    do {
      rng.set_seed(++request.input_seed);
      payment = make_payment_input(rng, 1, date + 1);
      choose_payment_customer(rng, payment);
    } while (!payment.by_name || payment.customer_warehouse != 2);
    const district::key paying_district(1, payment.district);
    write("district", paying_district, district_row);
    for (unsigned i = 1; i <= 4; ++i) {
      customer_name_idx::key key;
      key.c_w_id = payment.customer_warehouse;
      key.c_d_id = payment.customer_district;
      key.c_last.assign(payment.lastname, sizeof(payment.lastname));
      key.c_first.assign(std::string(1, 'A' + i));
      write("customer_name_idx", key, customer_name_idx::value(i));
      write("customer", customer::key(2, payment.customer_district, i), customer_row);
    }

    const history::key history_key(2, payment.customer_district, 2, payment.district, 1, date + 1);
    history::value history_row;
    attempt = worker.prepare(request);
    db.fail_commit = true;
    ALWAYS_ASSERT(attempt(checksum) == queued::Attempt::conflict);
    ALWAYS_ASSERT(!read("history", history_key, history_row));
    ALWAYS_ASSERT(read("warehouse", warehouse::key(1), warehouse_row) && warehouse_row.w_ytd == 0);
    ++request.high_sequence;
    ALWAYS_ASSERT(attempt(checksum) == queued::Attempt::committed);
    ALWAYS_ASSERT(read("history", history_key, history_row) && history_row.h_amount == payment.amount);
    ALWAYS_ASSERT(read("warehouse", warehouse::key(1), warehouse_row) && warehouse_row.w_ytd == payment.amount);
    ALWAYS_ASSERT(read("district", paying_district, district_row) && district_row.d_ytd == payment.amount);
    for (unsigned i = 1; i <= 4; ++i) {
      ALWAYS_ASSERT(read("customer", customer::key(2, payment.customer_district, i), customer_row));
      const float amount = i == 2 ? payment.amount : 0.0f;
      ALWAYS_ASSERT(customer_row.c_balance == -amount && customer_row.c_ytd_payment == amount);
      ALWAYS_ASSERT(customer_row.c_payment_cnt == (i == 2));
    }
    std::cout << "PASS queued Payment rollback, retry, remote lower median, balances and history\n";
  }
};

} // namespace

int main(int argc, char **)
{
  notification_cases::signal_backend() = argc > 1;
  nthreads = 2;
  scale_factor = 2;
  Fixture fixture;
  fixture.q2();
  fixture.short_transactions();
}
