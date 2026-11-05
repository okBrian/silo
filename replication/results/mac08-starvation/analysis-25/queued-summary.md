# Queued TPC-C

Latency cells show n / p50 / p90 / p99 / p99.9. † p99.9 uses fewer than 1,000 samples.

| Run | Status | Phase | Type | Generated | Committed | Rejected | Cancelled | Window commits | Drain completions | Drain commits | Queue delay (µs) | End-to-end latency (µs) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-starvation-25-bfo13vtz-r1-v1 | complete | measurement | All | 13,496 | 10,244 | 3,252 | 0 | 10,243 | 1 | 1 | 10,244 / 258.001 / 1,513.006 / 3,534.015 / 6,543.027 | 10,244 / 323.001 / 2,745.011 / 5,984.024 / 11,199.046 |
| queued-starvation-25-bfo13vtz-r1-v1 | complete | measurement | NewOrder | 6,018 | 4,386 | 1,632 | 0 | 4,386 | 0 | 0 | 4,386 / 230.001 / 958.004 / 2,927.012 / 17,553.071 | 4,386 / 318.001 / 1,041.004 / 3,020.013 / 18,036.073 |
| queued-starvation-25-bfo13vtz-r1-v1 | complete | measurement | Payment | 5,978 | 4,358 | 1,620 | 0 | 4,358 | 0 | 0 | 4,358 / 219.001 / 937.004 / 2,867.012 / 5,615.023 | 4,358 / 254.001 / 982.005 / 2,910.012 / 5,975.024 |
| queued-starvation-25-bfo13vtz-r1-v1 | complete | measurement | Q2 | 1,500 | 1,500 | 0 | 0 | 1,499 | 1 | 1 | 1,500 / 1,079.005 / 2,933.012 / 5,141.022 / 7,352.030 | 1,500 / 3,116.013 / 5,101.021 / 8,251.035 / 14,599.059 |
| queued-starvation-25-bfo13vtz-r1-v2 | complete | measurement | All | 26,339 | 14,874 | 11,465 | 0 | 14,872 | 2 | 2 | 14,874 / 510.003 / 2,992.015 / 6,587.033 / 11,901.060 | 14,874 / 729.004 / 4,101.021 / 9,672.049 / 14,339.072 |
| queued-starvation-25-bfo13vtz-r1-v2 | complete | measurement | NewOrder | 11,962 | 6,308 | 5,654 | 0 | 6,308 | 0 | 0 | 6,308 / 378.002 / 2,919.015 / 5,848.030 / 10,583.053 | 6,308 / 470.002 / 3,011.015 / 6,190.031 / 10,640.053 |
| queued-starvation-25-bfo13vtz-r1-v2 | complete | measurement | Payment | 12,030 | 6,219 | 5,811 | 0 | 6,219 | 0 | 0 | 6,219 / 392.002 / 2,945.015 / 6,245.032 / 12,040.060 | 6,219 / 423.002 / 2,978.015 / 6,296.032 / 12,109.060 |
| queued-starvation-25-bfo13vtz-r1-v2 | complete | measurement | Q2 | 2,347 | 2,347 | 0 | 0 | 2,345 | 2 | 2 | 2,347 / 1,499.008 / 4,086.021 / 8,355.042 / 12,389.062 | 2,347 / 3,690.019 / 8,209.042 / 13,412.069 / 16,802.085 |
