# Experiment collection report

MAC-08 emulated x86-64 Linux functional pressure test; monotonic ns budget

Measurement arrival cohorts; each row is one repetition, with no pooling. Latencies are committed requests including drain commits, in microseconds, using nearest rank. Throughput counts only commits before cutoff. The CSV also retains global/local queue and scheduled latency, all outcome counts, attempts, aborts, and sample counts.

Outstanding at cutoff includes pending global, queued, running/suspended, and due-but-not-generated requests. Unaccounted at exit is known only for a reconciled terminal trace. It excludes recorded cancellation/rejection; these remain separate outcomes. Unknown is never zero.

| run_id | policy | workers | repeat | status | reconciled | outstanding_at_cutoff | unaccounted_at_exit | issues |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-25-bfo13vtz-r1-v1 | cooperative | 1 | 1 | complete | True | 1 | 0 |  |
| queued-starvation-25-bfo13vtz-r1-v2 | cooperative | 2 | 1 | complete | True | 2 | 0 |  |

| Run | Policy | Workers | Type | Status | n | Latency p50/p90/p99/p99.9 (µs) | Queue p50/p90/p99/p99.9 (µs) | Window txn/s | Aborts | Not committed | p99.9 <1,000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-25-bfo13vtz-r1-v1 | cooperative | 1 | NewOrder | complete | 4386 | 318.001 / 1041.004 / 3020.013 / 18036.073 | 230.001 / 958.004 / 2927.012 / 17553.071 | 1462.0 | 0 | 1632 | False |
| queued-starvation-25-bfo13vtz-r1-v1 | cooperative | 1 | Payment | complete | 4358 | 254.001 / 982.005 / 2910.012 / 5975.024 | 219.001 / 937.004 / 2867.012 / 5615.023 | 1452.6666666666667 | 0 | 1620 | False |
| queued-starvation-25-bfo13vtz-r1-v1 | cooperative | 1 | Q2 | complete | 1500 | 3116.013 / 5101.021 / 8251.035 / 14599.059 | 1079.005 / 2933.012 / 5141.022 / 7352.030 | 499.6666666666667 | 0 | 0 | False |
| queued-starvation-25-bfo13vtz-r1-v2 | cooperative | 2 | NewOrder | complete | 6308 | 470.002 / 3011.015 / 6190.031 / 10640.053 | 378.002 / 2919.015 / 5848.030 / 10583.053 | 2102.6666666666665 | 0 | 5654 | False |
| queued-starvation-25-bfo13vtz-r1-v2 | cooperative | 2 | Payment | complete | 6219 | 423.002 / 2978.015 / 6296.032 / 12109.060 | 392.002 / 2945.015 / 6245.032 / 12040.060 | 2073.0 | 0 | 5811 | False |
| queued-starvation-25-bfo13vtz-r1-v2 | cooperative | 2 | Q2 | complete | 2347 | 3690.019 / 8209.042 / 13412.069 / 16802.085 | 1499.008 / 4086.021 / 8355.042 / 12389.062 | 781.6666666666666 | 0 | 0 | False |

Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.
