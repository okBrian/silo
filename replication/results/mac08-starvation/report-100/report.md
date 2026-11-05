# Experiment collection report

MAC-08 emulated x86-64 Linux functional pressure test; monotonic ns budget

Measurement arrival cohorts; each row is one repetition, with no pooling. Latencies are committed requests including drain commits, in microseconds, using nearest rank. Throughput counts only commits before cutoff. The CSV also retains global/local queue and scheduled latency, all outcome counts, attempts, aborts, and sample counts.

Outstanding at cutoff includes pending global, queued, running/suspended, and due-but-not-generated requests. Unaccounted at exit is known only for a reconciled terminal trace. It excludes recorded cancellation/rejection; these remain separate outcomes. Unknown is never zero.

| run_id | policy | workers | repeat | status | reconciled | outstanding_at_cutoff | unaccounted_at_exit | issues |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-100-eu99eesv-r1-v1 | cooperative | 1 | 1 | complete | True | 2 | 0 |  |
| queued-starvation-100-eu99eesv-r1-v2 | cooperative | 2 | 1 | complete | True | 8 | 0 |  |

| Run | Policy | Workers | Type | Status | n | Latency p50/p90/p99/p99.9 (µs) | Queue p50/p90/p99/p99.9 (µs) | Window txn/s | Aborts | Not committed | p99.9 <1,000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-100-eu99eesv-r1-v1 | cooperative | 1 | NewOrder | complete | 4396 | 314.003 / 599.005 / 3273.025 / 6110.047 | 224.002 / 501.004 / 3128.023 / 6029.046 | 1465.3333333333333 | 0 | 1622 | False |
| queued-starvation-100-eu99eesv-r1-v1 | cooperative | 1 | Payment | complete | 4358 | 253.002 / 533.004 / 3202.024 / 6524.050 | 219.002 / 487.004 / 3154.024 / 6468.050 | 1452.6666666666667 | 0 | 1620 | False |
| queued-starvation-100-eu99eesv-r1-v1 | cooperative | 1 | Q2 | complete | 1590 | 2972.022 / 4664.036 / 9931.076 / 17800.136 | 941.007 / 2310.017 / 5019.038 / 9745.074 | 529.3333333333334 | 0 | 0 | False |
| queued-starvation-100-eu99eesv-r1-v2 | cooperative | 2 | NewOrder | complete | 6170 | 492.004 / 3122.026 / 5833.049 / 9763.081 | 392.003 / 3008.025 / 5683.048 / 9242.077 | 2056.3333333333335 | 1 | 5792 | False |
| queued-starvation-100-eu99eesv-r1-v2 | cooperative | 2 | Payment | complete | 6244 | 434.004 / 3040.026 / 5575.046 / 9379.078 | 395.003 / 3002.025 / 5530.046 / 9341.078 | 2080.3333333333335 | 0 | 5786 | False |
| queued-starvation-100-eu99eesv-r1-v2 | cooperative | 2 | Q2 | complete | 2172 | 4343.036 / 8751.073 / 14412.120 / 19441.162 | 1805.015 / 4433.037 / 8579.072 / 15127.127 | 722.6666666666666 | 0 | 0 | False |

Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.
