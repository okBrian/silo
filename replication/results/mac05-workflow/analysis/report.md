# Experiment collection report

emulated x86-64 Linux functional validation on Colima/QEMU; Ubuntu 22.04; one-second smoke estimates

Measurement arrival cohorts; each row is one repetition, with no pooling. Latencies are committed requests including drain commits, in microseconds, using nearest rank. Throughput counts only commits before cutoff. The CSV also retains global/local queue and scheduled latency, all outcome counts, attempts, aborts, and sample counts.

Outstanding at cutoff includes pending global, queued, running/suspended, and due-but-not-generated requests. Unaccounted at exit is known only for a reconciled terminal trace. It excludes recorded cancellation/rejection; these remain separate outcomes. Unknown is never zero.

| run_id | policy | workers | repeat | status | reconciled | outstanding_at_cutoff | unaccounted_at_exit | issues |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-cooperative-smoke-_deb30he-r1-v1 | wait | 1 | 1 | complete | True | 2 | 0 |  |
| queued-cooperative-smoke-_deb30he-r1-v2 | cooperative | 1 | 1 | complete | True | 1 | 0 |  |
| queued-cooperative-smoke-_deb30he-r1-v3 | wait | 2 | 1 | complete | True | 4 | 0 |  |
| queued-cooperative-smoke-_deb30he-r1-v4 | cooperative | 2 | 1 | complete | True | 4 | 0 |  |
| queued-cooperative-smoke-_deb30he-r2-v4 | cooperative | 2 | 2 | complete | True | 3 | 0 |  |
| queued-cooperative-smoke-_deb30he-r2-v3 | wait | 2 | 2 | complete | True | 3 | 0 |  |
| queued-cooperative-smoke-_deb30he-r2-v2 | cooperative | 1 | 2 | complete | True | 1 | 0 |  |
| queued-cooperative-smoke-_deb30he-r2-v1 | wait | 1 | 2 | complete | True | 1 | 0 |  |

| Run | Policy | Workers | Type | Status | n | Latency p50/p90/p99/p99.9 (µs) | Queue p50/p90/p99/p99.9 (µs) | Window txn/s | Aborts | Not committed | p99.9 <1,000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| queued-cooperative-smoke-_deb30he-r1-v1 | wait | 1 | NewOrder | complete | 20 | 684.783 / 1701.461 / 2009.364 / 2009.364 | 569.820 / 1616.488 / 1786.435 / 1786.435 | 20.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v1 | wait | 1 | Payment | complete | 16 | 614.805 / 1696.463 / 1785.435 / 1785.435 | 555.824 / 1577.501 / 1684.467 / 1684.467 | 16.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v1 | wait | 1 | Q2 | complete | 720 | 1650.478 / 2600.177 / 3343.941 / 4551.559 | 312.901 / 1107.649 / 1775.438 / 2307.270 | 718.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v2 | cooperative | 1 | NewOrder | complete | 20 | 437.872 / 768.776 / 1368.601 / 1368.601 | 291.915 / 573.832 / 1094.681 / 1094.681 | 20.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v2 | cooperative | 1 | Payment | complete | 16 | 434.873 / 654.809 / 1090.682 / 1090.682 | 375.890 / 624.818 / 870.746 / 870.746 | 16.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v2 | cooperative | 1 | Q2 | complete | 670 | 1915.442 / 2978.132 / 4164.786 / 5082.519 | 458.866 / 1424.585 / 2041.405 / 3397.009 | 669.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v3 | wait | 2 | NewOrder | complete | 36 | 1020.741 / 2099.463 / 2293.413 / 2293.413 | 819.792 / 1948.506 / 2184.441 / 2184.441 | 36.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v3 | wait | 2 | Payment | complete | 36 | 778.801 / 2180.442 / 2404.386 / 2404.386 | 683.825 / 2098.464 / 2347.400 / 2347.400 | 36.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v3 | wait | 2 | Q2 | complete | 1090 | 2286.415 / 4498.850 / 9229.640 / 12075.912 | 627.839 / 2104.462 / 4652.811 / 6422.358 | 1086.0 | 0 | 0 | False |
| queued-cooperative-smoke-_deb30he-r1-v4 | cooperative | 2 | NewOrder | complete | 36 | 388.914 / 570.874 / 978.783 / 978.783 | 274.940 / 466.897 / 873.806 / 873.806 | 36.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v4 | cooperative | 2 | Payment | complete | 36 | 390.913 / 508.889 / 536.882 / 536.882 | 305.932 / 443.903 / 509.888 / 509.888 | 36.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r1-v4 | cooperative | 2 | Q2 | complete | 1188 | 2278.495 / 3731.173 / 7342.374 / 8905.028 | 607.866 / 1846.591 / 4544.994 / 5394.805 | 1184.0 | 0 | 0 | False |
| queued-cooperative-smoke-_deb30he-r2-v4 | cooperative | 2 | NewOrder | complete | 37 | 410.921 / 557.894 / 1500.713 / 1500.713 | 301.941 / 455.912 / 1382.736 / 1382.736 | 37.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v4 | cooperative | 2 | Payment | complete | 35 | 344.934 / 651.876 / 1587.696 / 1587.696 | 293.944 / 520.901 / 1565.701 / 1565.701 | 35.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v4 | cooperative | 2 | Q2 | complete | 1156 | 2237.568 / 4160.205 / 7753.518 / 9949.099 | 635.877 / 1902.633 / 4305.170 / 5232.000 | 1153.0 | 0 | 0 | False |
| queued-cooperative-smoke-_deb30he-r2-v3 | wait | 2 | NewOrder | complete | 37 | 739.877 / 2388.604 / 3691.388 / 3691.388 | 620.897 / 2285.621 / 3569.408 / 3569.408 | 37.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v3 | wait | 2 | Payment | complete | 35 | 672.889 / 1743.711 / 3571.408 / 3571.408 | 563.907 / 1718.715 / 3478.423 / 3478.423 | 35.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v3 | wait | 2 | Q2 | complete | 1255 | 1903.688 / 3493.421 / 6200.973 / 9504.425 | 459.925 / 1675.723 / 4080.323 / 5283.124 | 1252.0 | 0 | 0 | False |
| queued-cooperative-smoke-_deb30he-r2-v2 | cooperative | 1 | NewOrder | complete | 20 | 495.926 / 643.902 / 653.901 / 653.901 | 354.947 / 496.926 / 560.915 / 560.915 | 20.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v2 | cooperative | 1 | Payment | complete | 16 | 360.946 / 588.912 / 627.906 / 627.906 | 291.956 / 556.917 / 595.911 / 595.911 | 16.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v2 | cooperative | 1 | Q2 | complete | 690 | 1759.737 / 2854.574 / 4515.319 / 8505.717 | 361.945 / 1374.793 / 2068.688 / 4322.348 | 689.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v1 | wait | 1 | NewOrder | complete | 20 | 776.893 / 1503.792 / 1928.733 / 1928.733 | 607.916 / 1132.845 / 1828.747 / 1828.747 | 20.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v1 | wait | 1 | Payment | complete | 16 | 697.905 / 931.871 / 1669.768 / 1669.768 | 621.915 / 907.874 / 1506.791 / 1506.791 | 16.0 | 0 | 0 | True |
| queued-cooperative-smoke-_deb30he-r2-v1 | wait | 1 | Q2 | complete | 709 | 1664.772 / 2710.625 / 3743.481 / 4891.330 | 315.957 / 1163.839 / 1895.740 / 3313.546 | 708.0 | 0 | 0 | True |

Failed/incomplete runs are retained for diagnosis and must be excluded from performance claims.
