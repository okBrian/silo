#pragma once

#include <functional>

#include "bench_txn_context.h"
#include "tpcc.h"
#include "../txn.h"

// Q2 projects only the join columns from these three dimensions.
#define Q2_DIMENSION_KEY(x, y) x(uint32_t, id)
#define Q2_DIMENSION_VALUE(x, y) x(uint32_t, parent)
DO_STRUCT(q2_dimension, Q2_DIMENSION_KEY, Q2_DIMENSION_VALUE)
#undef Q2_DIMENSION_KEY
#undef Q2_DIMENSION_VALUE

namespace tpcc_q2 {

constexpr unsigned region_count = 5;
constexpr unsigned supplier_count = 10000;
constexpr unsigned supplier_scan_count = 100;
constexpr unsigned supplier_seed = 51271928;

class Tables {
public:
  Tables(abstract_db *db, const std::vector<abstract_ordered_index *> &stocks,
         abstract_ordered_index *items, unsigned item_count,
         unsigned scan_count = supplier_scan_count)
    : regions(db->open_index("q2_region", sizeof(q2_dimension::value))),
      nations(db->open_index("q2_nation", sizeof(q2_dimension::value))),
      suppliers(db->open_index("q2_supplier", sizeof(q2_dimension::value))),
      stocks(stocks), items(items), stock_map(scan_count)
  {
    ALWAYS_ASSERT(scan_count <= supplier_count);
    for (size_t w = 1; w <= stocks.size(); ++w) {
      for (unsigned i = 1; i <= item_count; ++i) {
        const auto supplier = (uint64_t(w) * i) % supplier_count;
        if (supplier < scan_count)
          stock_map[supplier].emplace_back(w, i);
      }
    }
  }

  void register_indexes(std::map<std::string, abstract_ordered_index *> &indexes) const
  {
    indexes["q2_region"] = regions;
    indexes["q2_nation"] = nations;
    indexes["q2_supplier"] = suppliers;
  }

  void load(abstract_db *db, str_arena &arena, void *buffer, util::fast_random &rng) const
  {
    // Region IDs from the audited reference, in ASCII nation-key order
    // (0-9, A-Z, a-z). Names and supplier addresses are unused by Q2.
    static const char nation_regions[] =
      "0111403322"
      "44240001234233122212230213"
      "33022122220040023323334323";
    static_assert(sizeof(nation_regions) == 63, "expected 62 nations");
    uint32_t nation_ids[62];
    for (unsigned i = 0; i < 62; ++i)
      nation_ids[i] = i < 10 ? '0' + i : i < 36 ? 'A' + i - 10 : 'a' + i - 36;

    for (unsigned i = 0; i < region_count; ++i)
      load_row(db, arena, buffer, regions, i, i);
    for (unsigned i = 0; i < 62; ++i)
      load_row(db, arena, buffer, nations, nation_ids[i], nation_regions[i] - '0');
    for (unsigned i = 0; i < supplier_count; ++i)
      load_row(db, arena, buffer, suppliers, i, nation_ids[rng.next_u32() % 62]);
  }

  bool run(abstract_db *db, bench_txn_context &state, unsigned target_region,
           uint64_t &checksum, const std::function<void()> &checkpoint = {}) const
  {
    scoped_str_arena arena_scope(state.arena);
    auto &key = state.str();
    auto &value = state.str();
    void *txn = db->new_txn(transaction_base::TXN_FLAG_READ_ONLY, state.arena, state.txn_buf());
    uint64_t result = UINT64_C(14695981039346656037);
    try {
      DimensionScan region_rows;
      DimensionScan nation_rows;
      regions->scan(txn, Encode(key, q2_dimension::key(0)), nullptr, region_rows, &state.arena);
      nations->scan(txn, Encode(key, q2_dimension::key(0)), nullptr, nation_rows, &state.arena);
      for (const auto &region : region_rows.rows) {
        if (region.first != target_region)
          continue;
        for (const auto &nation : nation_rows.rows) {
          if (nation.second != region.first)
            continue;
          for (unsigned s = 0; s < stock_map.size(); ++s) {
            // The index operation and its protection scope have returned.
            // Dimension scan callbacks still run inside the index operation.
            ALWAYS_ASSERT(suppliers->get(txn, Encode(key, q2_dimension::key(s)), value));
            if (checkpoint)
              checkpoint();
            q2_dimension::value supplier;
            Decode(value, supplier);
            if (supplier.parent != nation.first)
              continue;

            const stock::key *best = nullptr;
            int16_t min_qty = 0;
            for (const auto &candidate : stock_map[s]) {
              ALWAYS_ASSERT(stocks[candidate.s_w_id - 1]->get(txn, Encode(key, candidate), value));
              if (checkpoint)
                checkpoint();
              stock::value row;
              Decode(value, row);
              if (!best || row.s_quantity < min_qty) {
                best = &candidate;
                min_qty = row.s_quantity;
              }
            }
            if (!best)
              continue;
            ALWAYS_ASSERT(items->get(txn, Encode(key, item::key(best->s_i_id)), value));
            if (checkpoint)
              checkpoint();
            item::value row;
            Decode(value, row);
            if (row.i_data.str().find('b') != std::string::npos)
              continue;
            for (uint32_t field : {target_region, nation.first, s, uint32_t(best->s_w_id),
                                   uint32_t(best->s_i_id), uint32_t(min_qty)})
              hash_field(result, field);
          }
        }
      }

      if (db->commit_txn(txn)) {
        checksum = result;
        return true;
      }
    } catch (const abstract_db::abstract_abort_exception &) {
      db->abort_txn(txn);
      return false;
    } catch (...) {
      db->abort_txn(txn);
      throw;
    }
    db->abort_txn(txn);
    return false;
  }

private:
  class DimensionScan : public abstract_ordered_index::scan_callback {
  public:
    bool invoke(const char *key, size_t, const std::string &value) OVERRIDE
    {
      q2_dimension::key k;
      q2_dimension::value v;
      Decode(key, k);
      Decode(value, v);
      rows.emplace_back(uint32_t(k.id), uint32_t(v.parent));
      return true;
    }

    std::vector<std::pair<uint32_t, uint32_t>> rows;
  };

  static void load_row(abstract_db *db, str_arena &arena, void *buffer,
                       abstract_ordered_index *index, uint32_t id, uint32_t parent)
  {
    scoped_str_arena arena_scope(arena);
    auto &key = *arena.next();
    auto &value = *arena.next();
    void *txn = db->new_txn(0, arena, buffer);
    index->insert(txn, Encode(key, q2_dimension::key(id)), Encode(value, q2_dimension::value(parent)));
    ALWAYS_ASSERT(db->commit_txn(txn));
  }

  static void hash_field(uint64_t &hash, uint32_t field)
  {
    for (unsigned i = 0; i < 4; ++i) {
      hash = (hash ^ (field & 255)) * UINT64_C(1099511628211);
      field >>= 8;
    }
  }

  abstract_ordered_index *const regions;
  abstract_ordered_index *const nations;
  abstract_ordered_index *const suppliers;
  const std::vector<abstract_ordered_index *> stocks;
  abstract_ordered_index *const items;
  std::vector<std::vector<stock::key>> stock_map;
};

} // namespace tpcc_q2
