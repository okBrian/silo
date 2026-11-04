# Stock TPC-C

Each latency cell averages the reported run means with equal weight.

| Configuration | Warehouses | Seconds | Workers | Variant | Runs | Planned | Throughput mean ± sample SD (txn/s) | Throughput range (txn/s) | Average of timed successful attempts (µs) | Mean aborts/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stock-tpcc-2wh | 2 | 30 | 1 | 1 | 5 | 5 | 84,254 ± 8,538 | 70,944–94,629 | 11.94 | 0.00 |
| stock-tpcc-2wh | 2 | 30 | 2 | 2 | 5 | 5 | 148,330 ± 13,622 | 136,536–168,677 | 13.53 | 6.30 |
