# Experiment collection report

Linux-signal backend in emulated Linux; local functional pressure; monotonic ns

Measurement arrival cohorts; each row is one repetition, with no pooling. Latencies are committed requests including drain commits, in microseconds, using nearest rank. Throughput counts only commits before cutoff. The CSV also retains global/local queue and scheduled latency, all outcome counts, attempts, aborts, and sample counts.

Outstanding at cutoff includes pending global, queued, running/suspended, and due-but-not-generated requests. Unaccounted at exit is known only for a reconciled terminal trace. It excludes recorded cancellation/rejection; these remain separate outcomes. Unknown is never zero.

| run_id | policy | workers | repeat | status | reconciled | outstanding_at_cutoff | unaccounted_at_exit | issues |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-signal-100-r36o_fcu-r1-v1 | linux-signal | 1 | 1 | complete | True | 2 | 0 |  |
| queued-signal-100-r36o_fcu-r1-v2 | linux-signal | 2 | 1 | complete | True | 12 | 0 |  |

| Run | Policy | Workers | Type | Status | n | Latency p50/p90/p99/p99.9 (µs) | Queue p50/p90/p99/p99.9 (µs) | Window txn/s | Aborts | Not committed | p99.9 <1,000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-signal-100-r36o_fcu-r1-v1 | linux-signal | 1 | NewOrder | complete | 4520 | 338.004 / 576.006 / 2959.035 / 5803.069 | 225.003 / 460.005 / 2889.034 / 5747.068 | 1506.6666666666667 | 0 | 1498 | False |
| queued-signal-100-r36o_fcu-r1-v1 | linux-signal | 1 | Payment | complete | 4440 | 268.003 / 517.006 / 2774.033 / 4214.050 | 217.003 / 463.005 / 2716.032 / 4128.049 | 1480.0 | 0 | 1538 | False |
| queued-signal-100-r36o_fcu-r1-v1 | linux-signal | 1 | Q2 | complete | 1415 | 3496.041 / 5223.062 / 8127.096 / 11159.131 | 1240.015 / 2997.036 / 4583.054 / 8360.098 | 471.0 | 0 | 0 | False |
| queued-signal-100-r36o_fcu-r1-v2 | linux-signal | 2 | NewOrder | complete | 6160 | 501.006 / 3173.038 / 5931.071 / 10877.130 | 401.005 / 3068.036 / 5780.069 / 10821.129 | 2051.6666666666665 | 0 | 5802 | False |
| queued-signal-100-r36o_fcu-r1-v2 | linux-signal | 2 | Payment | complete | 6123 | 429.005 / 3098.037 / 5723.068 / 10725.127 | 389.004 / 3054.036 / 5681.068 / 10690.127 | 2040.0 | 0 | 5907 | False |
| queued-signal-100-r36o_fcu-r1-v2 | linux-signal | 2 | Q2 | complete | 2155 | 4340.052 / 8826.105 / 14149.168 / 18840.224 | 1792.021 / 4551.054 / 8740.104 / 13586.162 | 717.0 | 0 | 0 | False |

Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.
