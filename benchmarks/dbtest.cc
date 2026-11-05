#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <string>
#include <set>
#include <stdexcept>

#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include "../allocator.h"
#include "../stats_server.h"
#include "bench.h"
#include "bdb_wrapper.h"
#include "ndb_wrapper.h"
#include "ndb_wrapper_impl.h"
#include "kvdb_wrapper.h"
#include "kvdb_wrapper_impl.h"
#if !NO_MYSQL
#include "mysql_wrapper.h"
#endif

using namespace std;
using namespace util;

enum {
  QUEUED_RUN = 1000, QUEUED_BATCH, QUEUED_INTERVAL, QUEUED_GLOBAL, QUEUED_WORKER,
  QUEUED_SEED, QUEUED_WARMUP, QUEUED_DRAIN, QUEUED_MIXED, QUEUED_POLICY, QUEUED_YIELD_INTERVAL, QUEUED_STARVATION
};

static uint64_t
queued_number(const char *arg, const char *option, uint64_t min, uint64_t max)
{
  char *end;
  errno = 0;
  const uint64_t value = strtoull(arg, &end, 10);
  if (*arg < '0' || *arg > '9' || *end || errno || value < min || value > max) {
    cerr << "[ERROR] invalid --" << option << ": " << arg << endl;
    exit(1);
  }
  return value;
}

static vector<string>
split_ws(const string &s)
{
  vector<string> r;
  istringstream iss(s);
  copy(istream_iterator<string>(iss),
       istream_iterator<string>(),
       back_inserter<vector<string>>(r));
  return r;
}

static size_t
parse_memory_spec(const string &s)
{
  string x(s);
  size_t mult = 1;
  if (x.back() == 'G') {
    mult = static_cast<size_t>(1) << 30;
    x.pop_back();
  } else if (x.back() == 'M') {
    mult = static_cast<size_t>(1) << 20;
    x.pop_back();
  } else if (x.back() == 'K') {
    mult = static_cast<size_t>(1) << 10;
    x.pop_back();
  }
  return strtoul(x.c_str(), nullptr, 10) * mult;
}

int
main(int argc, char **argv)
{
  abstract_db *db = NULL;
  void (*test_fn)(abstract_db *, int argc, char **argv) = NULL;
  string bench_type = "ycsb";
  string db_type = "ndb-proto2";
  char *curdir = get_current_dir_name();
  string basedir = curdir;
  string bench_opts;
  size_t numa_memory = 0;
  free(curdir);
  int saw_run_spec = 0;
  int nofsync = 0;
  int do_compress = 0;
  int fake_writes = 0;
  int disable_gc = 0;
  int disable_snapshots = 0;
  vector<string> logfiles;
  vector<vector<unsigned>> assignments;
  string stats_server_sockfile;
  size_t queued_batch = 0;
  size_t queued_global = 0;
  bool saw_queue_setting = false;
  bool queued_mixed = false;
  bool saw_yield_interval = false;
  bool saw_starvation = false;
  while (1) {
    static struct option long_options[] =
    {
      {"verbose"                    , no_argument       , &verbose                   , 1}   ,
      {"parallel-loading"           , no_argument       , &enable_parallel_loading   , 1}   ,
      {"pin-cpus"                   , no_argument       , &pin_cpus                  , 1}   ,
      {"slow-exit"                  , no_argument       , &slow_exit                 , 1}   ,
      {"retry-aborted-transactions" , no_argument       , &retry_aborted_transaction , 1}   ,
      {"backoff-aborted-transactions" , no_argument     , &backoff_aborted_transaction , 1}   ,
      {"bench"                      , required_argument , 0                          , 'b'} ,
      {"scale-factor"               , required_argument , 0                          , 's'} ,
      {"num-threads"                , required_argument , 0                          , 't'} ,
      {"db-type"                    , required_argument , 0                          , 'd'} ,
      {"basedir"                    , required_argument , 0                          , 'B'} ,
      {"txn-flags"                  , required_argument , 0                          , 'f'} ,
      {"runtime"                    , required_argument , 0                          , 'r'} ,
      {"ops-per-worker"             , required_argument , 0                          , 'n'} ,
      {"bench-opts"                 , required_argument , 0                          , 'o'} ,
      {"numa-memory"                , required_argument , 0                          , 'm'} , // implies --pin-cpus
      {"logfile"                    , required_argument , 0                          , 'l'} ,
      {"assignment"                 , required_argument , 0                          , 'a'} ,
      {"log-nofsync"                , no_argument       , &nofsync                   , 1}   ,
      {"log-compress"               , no_argument       , &do_compress               , 1}   ,
      {"log-fake-writes"            , no_argument       , &fake_writes               , 1}   ,
      {"disable-gc"                 , no_argument       , &disable_gc                , 1}   ,
      {"disable-snapshots"          , no_argument       , &disable_snapshots         , 1}   ,
      {"stats-server-sockfile"      , required_argument , 0                          , 'x'} ,
      {"no-reset-counters"          , no_argument       , &no_reset_counters         , 1}   ,
      {"queued"                    , required_argument , 0                          , QUEUED_RUN},
      {"queued-batch-size"         , required_argument , 0                          , QUEUED_BATCH},
      {"queued-interval-us"        , required_argument , 0                          , QUEUED_INTERVAL},
      {"queued-global-capacity"    , required_argument , 0                          , QUEUED_GLOBAL},
      {"queued-worker-capacity"    , required_argument , 0                          , QUEUED_WORKER},
      {"queued-seed"               , required_argument , 0                          , QUEUED_SEED},
      {"queued-warmup"             , required_argument , 0                          , QUEUED_WARMUP},
      {"queued-drain"              , required_argument , 0                          , QUEUED_DRAIN},
      {"queued-mixed"              , no_argument       , 0                          , QUEUED_MIXED},
      {"queued-policy"             , required_argument , 0                          , QUEUED_POLICY},
      {"queued-yield-interval"     , required_argument , 0                          , QUEUED_YIELD_INTERVAL},
      {"queued-starvation-threshold", required_argument, 0, QUEUED_STARVATION},
      {0, 0, 0, 0}
    };
    int option_index = 0;
    int c = getopt_long(argc, argv, "b:s:t:d:B:f:r:n:o:m:l:a:x:", long_options, &option_index);
    if (c == -1)
      break;

    if (c > QUEUED_RUN && c <= QUEUED_STARVATION)
      saw_queue_setting = true;
    switch (c) {
    case QUEUED_RUN:
      queued_run_id = optarg;
      if (queued_run_id.empty() || queued_run_id.find_first_of("\t\r\n") != string::npos) {
        cerr << "[ERROR] --queued requires a nonempty TSV-safe run ID" << endl;
        return 1;
      }
      break;

    case QUEUED_BATCH:
      queued_batch = queued_number(optarg, long_options[option_index].name, 1, UINT32_MAX);
      break;
    case QUEUED_INTERVAL:
      queued_config.interval = queued_number(optarg, long_options[option_index].name,
                                             1, UINT32_MAX) * 1000;
      break;
    case QUEUED_GLOBAL:
      queued_global = queued_number(optarg, long_options[option_index].name, 1, UINT32_MAX);
      break;
    case QUEUED_WORKER:
      queued_config.high_capacity = queued_number(optarg, long_options[option_index].name,
                                                  1, UINT32_MAX);
      break;
    case QUEUED_SEED:
      queued_config.seed = queued_number(optarg, long_options[option_index].name, 0, UINT64_MAX);
      break;
    case QUEUED_WARMUP:
      queued_warmup_seconds = queued_number(optarg, long_options[option_index].name, 0, 86400);
      break;
    case QUEUED_DRAIN:
      queued_drain_seconds = queued_number(optarg, long_options[option_index].name, 0, 86400);
      break;
    case QUEUED_MIXED:
      queued_mixed = true;
      break;
    case QUEUED_POLICY:
      if (string(optarg) == "wait")
        queued_config.policy = queued::Policy::wait;
      else if (string(optarg) == "cooperative")
        queued_config.policy = queued::Policy::cooperative;
      else {
        cerr << "[ERROR] --queued-policy must be wait or cooperative" << endl;
        return 1;
      }
      break;
    case QUEUED_STARVATION:
      saw_starvation = true;
      queued_config.starvation_threshold = queued_number(optarg, long_options[option_index].name, 0, 100);
      break;
    case QUEUED_YIELD_INTERVAL:
      saw_yield_interval = true;
      queued_config.yield_interval = queued_number(optarg, long_options[option_index].name,
                                                   1, UINT32_MAX);
      break;

    case 0:
      if (long_options[option_index].flag != 0)
        break;
      abort();
      break;

    case 'b':
      bench_type = optarg;
      break;

    case 's':
      scale_factor = strtod(optarg, NULL);
      ALWAYS_ASSERT(scale_factor > 0.0);
      break;

    case 't':
      nthreads = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(nthreads > 0);
      break;

    case 'd':
      db_type = optarg;
      break;

    case 'B':
      basedir = optarg;
      break;

    case 'f':
      txn_flags = strtoul(optarg, NULL, 10);
      break;

    case 'r':
      ALWAYS_ASSERT(!saw_run_spec);
      saw_run_spec = 1;
      runtime = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(runtime > 0);
      run_mode = RUNMODE_TIME;
      break;

    case 'n':
      ALWAYS_ASSERT(!saw_run_spec);
      saw_run_spec = 1;
      ops_per_worker = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(ops_per_worker > 0);
      run_mode = RUNMODE_OPS;

    case 'o':
      bench_opts = optarg;
      break;

    case 'm':
      {
        pin_cpus = 1;
        const size_t m = parse_memory_spec(optarg);
        ALWAYS_ASSERT(m > 0);
        numa_memory = m;
      }
      break;

    case 'l':
      logfiles.emplace_back(optarg);
      break;

    case 'a':
      assignments.emplace_back(
          ParseCSVString<unsigned, RangeAwareParser<unsigned>>(optarg));
      break;

    case 'x':
      stats_server_sockfile = optarg;
      break;

    case '?':
      /* getopt_long already printed an error message. */
      exit(1);

    default:
      abort();
    }
  }

  if (queued_run_id.empty() && saw_queue_setting) {
    cerr << "[ERROR] --queued-* settings require --queued RUN_ID" << endl;
    return 1;
  }
  if (!queued_run_id.empty()) {
    if (saw_starvation && !queued_mixed) {
      cerr << "[ERROR] starvation threshold requires --queued-mixed" << endl;
      return 1;
    }
    if (queued_config.policy == queued::Policy::cooperative) {
#ifndef SILO_WORKER_CONTEXTS
      cerr << "[ERROR] Cooperative requires a WORKER_CONTEXTS=1 build" << endl;
      return 1;
#endif
      if (!queued_mixed) {
        cerr << "[ERROR] Cooperative requires --queued-mixed" << endl;
        return 1;
      }
    } else if (saw_yield_interval) {
      cerr << "[ERROR] --queued-yield-interval requires --queued-policy cooperative" << endl;
      return 1;
    }
    if (bench_type != "tpcc" || db_type != "ndb-proto2" ||
        scale_factor != double(nthreads) || nthreads > UINT32_MAX / 4 ||
        run_mode != RUNMODE_TIME || runtime > 86400 || !logfiles.empty() ||
        retry_aborted_transaction || backoff_aborted_transaction || slow_exit) {
      cerr << "[ERROR] --queued requires time-based TPC-C on ndb-proto2 with one "
              "warehouse per worker, logging disabled, and no stock retry/backoff or slow-exit flags"
           << endl;
      return 1;
    }
    queued_config.workers = nthreads;
    queued_config.batch_size = queued_batch ? queued_batch : 4 * nthreads;
    queued_config.global_capacity = queued_global ? queued_global : 4 * nthreads;
    if (queued_mixed && (disable_snapshots || txn_flags != 0)) {
      cerr << "[ERROR] --queued-mixed requires protocol-2 snapshots and default transaction flags" << endl;
      return 1;
    }
    queued_config.generate_low = queued_mixed;
    queued_config.low_kind = queued::Kind::q2;
    const auto batches = [](uint64_t seconds) {
      return seconds ? (seconds * queued::second - 1) / queued_config.interval : 0;
    };
    if (batches(runtime) == 0 || batches(runtime) + batches(queued_warmup_seconds) >
        (UINT64_C(1) << 31) / queued_config.batch_size) {
      cerr << "[ERROR] --queued requires at least one measured batch and at most "
              "2^31 total high-priority requests" << endl;
      return 1;
    }
  }

  if (bench_type == "ycsb")
    test_fn = ycsb_do_test;
  else if (bench_type == "tpcc")
    test_fn = tpcc_do_test;
  else if (bench_type == "queue")
    test_fn = queue_do_test;
  else if (bench_type == "encstress")
    test_fn = encstress_do_test;
  else if (bench_type == "bid")
    test_fn = bid_do_test;
  else
    ALWAYS_ASSERT(false);

  if (do_compress && logfiles.empty()) {
    cerr << "[ERROR] --log-compress specified without logging enabled" << endl;
    return 1;
  }

  if (fake_writes && logfiles.empty()) {
    cerr << "[ERROR] --log-fake-writes specified without logging enabled" << endl;
    return 1;
  }

  if (nofsync && logfiles.empty()) {
    cerr << "[ERROR] --log-nofsync specified without logging enabled" << endl;
    return 1;
  }

  if (fake_writes && nofsync) {
    cerr << "[WARNING] --log-nofsync has no effect with --log-fake-writes enabled" << endl;
  }

#ifndef ENABLE_EVENT_COUNTERS
  if (!stats_server_sockfile.empty()) {
    cerr << "[WARNING] --stats-server-sockfile with no event counters enabled is useless" << endl;
  }
#endif

  // initialize the numa allocator
  if (numa_memory > 0) {
    const size_t maxpercpu = util::iceil(
        numa_memory / nthreads, ::allocator::GetHugepageSize());
    numa_memory = maxpercpu * nthreads;
    ::allocator::Initialize(nthreads, maxpercpu);
  }

  const set<string> can_persist({"ndb-proto2"});
  if (!logfiles.empty() && !can_persist.count(db_type)) {
    cerr << "[ERROR] benchmark " << db_type
         << " does not have persistence implemented" << endl;
    return 1;
  }

#ifdef PROTO2_CAN_DISABLE_GC
  const set<string> has_gc({"ndb-proto1", "ndb-proto2"});
  if (disable_gc && !has_gc.count(db_type)) {
    cerr << "[ERROR] benchmark " << db_type
         << " does not have gc to disable" << endl;
    return 1;
  }
#else
  if (disable_gc) {
    cerr << "[ERROR] macro PROTO2_CAN_DISABLE_GC was not set, cannot disable gc" << endl;
    return 1;
  }
#endif

#ifdef PROTO2_CAN_DISABLE_SNAPSHOTS
  const set<string> has_snapshots({"ndb-proto2"});
  if (disable_snapshots && !has_snapshots.count(db_type)) {
    cerr << "[ERROR] benchmark " << db_type
         << " does not have snapshots to disable" << endl;
    return 1;
  }
#else
  if (disable_snapshots) {
    cerr << "[ERROR] macro PROTO2_CAN_DISABLE_SNAPSHOTS was not set, cannot disable snapshots" << endl;
    return 1;
  }
#endif

  if (db_type == "bdb") {
    const string cmd = "rm -rf " + basedir + "/db/*";
    // XXX(stephentu): laziness
    int ret UNUSED = system(cmd.c_str());
    db = new bdb_wrapper("db", bench_type + ".db");
  } else if (db_type == "ndb-proto1") {
    // XXX: hacky simulation of proto1
    db = new ndb_wrapper<transaction_proto2>(
        logfiles, assignments, !nofsync, do_compress, fake_writes);
    transaction_proto2_static::set_hack_status(true);
    ALWAYS_ASSERT(transaction_proto2_static::get_hack_status());
#ifdef PROTO2_CAN_DISABLE_GC
    if (!disable_gc)
      transaction_proto2_static::InitGC();
#endif
  } else if (db_type == "ndb-proto2") {
    db = new ndb_wrapper<transaction_proto2>(
        logfiles, assignments, !nofsync, do_compress, fake_writes);
    ALWAYS_ASSERT(!transaction_proto2_static::get_hack_status());
#ifdef PROTO2_CAN_DISABLE_GC
    if (!disable_gc)
      transaction_proto2_static::InitGC();
#endif
#ifdef PROTO2_CAN_DISABLE_SNAPSHOTS
    if (disable_snapshots)
      transaction_proto2_static::DisableSnapshots();
#endif
  } else if (db_type == "kvdb") {
    db = new kvdb_wrapper<true>;
  } else if (db_type == "kvdb-st") {
    db = new kvdb_wrapper<false>;
#if !NO_MYSQL
  } else if (db_type == "mysql") {
    string dbdir = basedir + "/mysql-db";
    db = new mysql_wrapper(dbdir, bench_type);
#endif
  } else
    ALWAYS_ASSERT(false);

#ifdef DEBUG
  cerr << "WARNING: benchmark built in DEBUG mode!!!" << endl;
#endif

#ifdef CHECK_INVARIANTS
  cerr << "WARNING: invariant checking is enabled - should disable for benchmark" << endl;
#ifdef PARANOID_CHECKING
  cerr << "  *** Paranoid checking is enabled ***" << endl;
#endif
#endif

  if (verbose) {
    const unsigned long ncpus = coreid::num_cpus_online();
    cerr << "Database Benchmark:"                           << endl;
    cerr << "  pid: " << getpid()                           << endl;
    cerr << "settings:"                                     << endl;
    cerr << "  par-loading : " << enable_parallel_loading   << endl;
    cerr << "  pin-cpus    : " << pin_cpus                  << endl;
    cerr << "  slow-exit   : " << slow_exit                 << endl;
    cerr << "  retry-txns  : " << retry_aborted_transaction << endl;
    cerr << "  backoff-txns: " << backoff_aborted_transaction << endl;
    cerr << "  bench       : " << bench_type                << endl;
    cerr << "  scale       : " << scale_factor              << endl;
    cerr << "  num-cpus    : " << ncpus                     << endl;
    cerr << "  num-threads : " << nthreads                  << endl;
    cerr << "  db-type     : " << db_type                   << endl;
    cerr << "  basedir     : " << basedir                   << endl;
    cerr << "  txn-flags   : " << hexify(txn_flags)         << endl;
    if (run_mode == RUNMODE_TIME)
      cerr << "  runtime     : " << runtime                 << endl;
    else
      cerr << "  ops/worker  : " << ops_per_worker          << endl;
#ifdef USE_VARINT_ENCODING
    cerr << "  var-encode  : yes"                           << endl;
#else
    cerr << "  var-encode  : no"                            << endl;
#endif

#ifdef USE_JEMALLOC
    cerr << "  allocator   : jemalloc"                      << endl;
#elif defined USE_TCMALLOC
    cerr << "  allocator   : tcmalloc"                      << endl;
#elif defined USE_FLOW
    cerr << "  allocator   : flow"                          << endl;
#else
    cerr << "  allocator   : libc"                          << endl;
#endif
    if (numa_memory > 0) {
      cerr << "  numa-memory : " << numa_memory             << endl;
    } else {
      cerr << "  numa-memory : disabled"                    << endl;
    }
    cerr << "  logfiles : " << logfiles                     << endl;
    cerr << "  assignments : " << assignments               << endl;
    cerr << "  disable-gc : " << disable_gc                 << endl;
    cerr << "  disable-snapshots : " << disable_snapshots   << endl;
    cerr << "  stats-server-sockfile: " << stats_server_sockfile << endl;

    cerr << "system properties:" << endl;
    cerr << "  btree_internal_node_size: " << concurrent_btree::InternalNodeSize() << endl;
    cerr << "  btree_leaf_node_size    : " << concurrent_btree::LeafNodeSize() << endl;

#ifdef TUPLE_PREFETCH
    cerr << "  tuple_prefetch          : yes" << endl;
#else
    cerr << "  tuple_prefetch          : no" << endl;
#endif

#ifdef BTREE_NODE_PREFETCH
    cerr << "  btree_node_prefetch     : yes" << endl;
#else
    cerr << "  btree_node_prefetch     : no" << endl;
#endif

  }

  if (!stats_server_sockfile.empty()) {
    stats_server *srvr = new stats_server(stats_server_sockfile);
    thread(&stats_server::serve_forever, srvr).detach();
  }

  vector<string> bench_toks = split_ws(bench_opts);
  int argc = 1 + bench_toks.size();
  char *argv[argc];
  argv[0] = (char *) bench_type.c_str();
  for (size_t i = 1; i <= bench_toks.size(); i++)
    argv[i] = (char *) bench_toks[i - 1].c_str();
  try {
    test_fn(db, argc, argv);
  } catch (const std::exception &error) {
    if (queued_run_id.empty())
      throw;
    cerr << "[ERROR] " << error.what() << endl;
    delete db;
    return 1;
  }
  delete db;
  return 0;
}
