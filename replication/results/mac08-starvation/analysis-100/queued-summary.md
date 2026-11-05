# Queued TPC-C

Latency cells show n / p50 / p90 / p99 / p99.9. † p99.9 uses fewer than 1,000 samples.

| Run | Status | Phase | Type | Generated | Committed | Rejected | Cancelled | Window commits | Drain completions | Drain commits | Queue delay (µs) | End-to-end latency (µs) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-100-eu99eesv-r1-v1 | complete | measurement | All | 13,586 | 10,344 | 3,242 | 0 | 10,342 | 2 | 2 | 10,344 / 252.002 / 1,564.012 / 3,424.026 / 6,901.052 | 10,344 / 317.003 / 2,916.022 / 5,450.041 / 10,153.077 |
| queued-starvation-100-eu99eesv-r1-v1 | complete | measurement | NewOrder | 6,018 | 4,396 | 1,622 | 0 | 4,396 | 0 | 0 | 4,396 / 224.002 / 501.004 / 3,128.023 / 6,029.046 | 4,396 / 314.003 / 599.005 / 3,273.025 / 6,110.047 |
| queued-starvation-100-eu99eesv-r1-v1 | complete | measurement | Payment | 5,978 | 4,358 | 1,620 | 0 | 4,358 | 0 | 0 | 4,358 / 219.002 / 487.004 / 3,154.024 / 6,468.050 | 4,358 / 253.002 / 533.004 / 3,202.024 / 6,524.050 |
| queued-starvation-100-eu99eesv-r1-v1 | complete | measurement | Q2 | 1,590 | 1,590 | 0 | 0 | 1,588 | 2 | 2 | 1,590 / 941.007 / 2,310.017 / 5,019.038 / 9,745.074 | 1,590 / 2,972.022 / 4,664.036 / 9,931.076 / 17,800.136 |
| queued-starvation-100-eu99eesv-r1-v2 | complete | measurement | All | 26,164 | 14,586 | 11,578 | 0 | 14,578 | 8 | 8 | 14,586 / 487.004 / 3,134.026 / 6,321.053 / 10,215.085 | 14,586 / 616.005 / 4,370.036 / 10,007.084 / 15,422.129 |
| queued-starvation-100-eu99eesv-r1-v2 | complete | measurement | NewOrder | 11,962 | 6,170 | 5,792 | 0 | 6,169 | 1 | 1 | 6,170 / 392.003 / 3,008.025 / 5,683.048 / 9,242.077 | 6,170 / 492.004 / 3,122.026 / 5,833.049 / 9,763.081 |
| queued-starvation-100-eu99eesv-r1-v2 | complete | measurement | Payment | 12,030 | 6,244 | 5,786 | 0 | 6,241 | 3 | 3 | 6,244 / 395.003 / 3,002.025 / 5,530.046 / 9,341.078 | 6,244 / 434.004 / 3,040.026 / 5,575.046 / 9,379.078 |
| queued-starvation-100-eu99eesv-r1-v2 | complete | measurement | Q2 | 2,172 | 2,172 | 0 | 0 | 2,168 | 4 | 4 | 2,172 / 1,805.015 / 4,433.037 / 8,579.072 / 15,127.127 | 2,172 / 4,343.036 / 8,751.073 / 14,412.120 / 19,441.162 |
