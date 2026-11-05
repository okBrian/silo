# Experiment collection report

Linux-signal backend in emulated Linux; local functional pressure; monotonic ns

Measurement arrival cohorts; each row is one repetition, with no pooling. Latencies are committed requests including drain commits, in microseconds, using nearest rank. Throughput counts only commits before cutoff. The CSV also retains global/local queue and scheduled latency, all outcome counts, attempts, aborts, and sample counts.

Outstanding at cutoff includes pending global, queued, running/suspended, and due-but-not-generated requests. Unaccounted at exit is known only for a reconciled terminal trace. It excludes recorded cancellation/rejection; these remain separate outcomes. Unknown is never zero.

| run_id | policy | workers | repeat | status | reconciled | outstanding_at_cutoff | unaccounted_at_exit | issues |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-signal-25-ipz89ghd-r1-v1 | linux-signal | 1 | 1 | complete | True | 5 | 0 |  |
| queued-signal-25-ipz89ghd-r1-v2 | linux-signal | 2 | 1 | complete | True | 12 | 0 |  |

| Run | Policy | Workers | Type | Status | n | Latency p50/p90/p99/p99.9 (µs) | Queue p50/p90/p99/p99.9 (µs) | Window txn/s | Aborts | Not committed | p99.9 <1,000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-signal-25-ipz89ghd-r1-v1 | linux-signal | 1 | NewOrder | complete | 3887 | 482.006 / 1935.022 / 4206.049 / 16142.187 | 370.004 / 1816.021 / 4074.047 / 15767.183 | 1295.0 | 0 | 2131 | False |
| queued-signal-25-ipz89ghd-r1-v1 | linux-signal | 1 | Payment | complete | 3806 | 381.005 / 1689.020 / 3999.046 / 6714.078 | 326.004 / 1629.019 / 3701.043 / 6643.078 | 1268.0 | 0 | 2172 | False |
| queued-signal-25-ipz89ghd-r1-v1 | linux-signal | 1 | Q2 | complete | 1181 | 4095.047 / 6913.081 / 11151.130 / 19155.222 | 1730.020 / 3680.043 / 7220.084 / 11935.138 | 393.3333333333333 | 0 | 0 | False |
| queued-signal-25-ipz89ghd-r1-v2 | linux-signal | 2 | NewOrder | complete | 5430 | 1552.018 / 4049.047 / 9939.117 / 15905.186 | 1429.016 / 3900.045 / 9708.114 / 15610.182 | 1808.3333333333333 | 0 | 6532 | False |
| queued-signal-25-ipz89ghd-r1-v2 | linux-signal | 2 | Payment | complete | 5383 | 1491.017 / 4004.047 / 10223.120 / 19725.231 | 1433.017 / 3940.046 / 9678.113 / 19655.230 | 1793.3333333333333 | 0 | 6647 | False |
| queued-signal-25-ipz89ghd-r1-v2 | linux-signal | 2 | Q2 | complete | 1826 | 4770.056 / 10615.124 / 19368.226 / 28047.329 | 2062.024 / 5116.060 / 11015.129 / 25479.298 | 607.3333333333334 | 0 | 0 | False |

Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.
