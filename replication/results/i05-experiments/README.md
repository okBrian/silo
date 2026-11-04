# I05 regenerated evidence

- [Stock baseline table](stock-summary.md) and [throughput chart](figures/stock-throughput-1.svg). All ten sets of raw metrics and both summary rows match the [published baseline](../i04-pace-baseline/SUMMARY.md). The measurements came from [PACE job 6791465](../pace-runs/6791465/).
- [Queued smoke table](queued-summary.md), [queue delay chart](figures/queued-queue-1.svg), and [end-to-end latency chart](figures/queued-end_to_end-1.svg). The [I06 smoke runs](../i06-request-driver/README.md) contributed 36 and 72 measured commits. The one-worker smoke also recorded 36 warm-up commits. Measured rejects, cancellations and drain completions were zero in both runs. All displayed p99.9 estimates are flagged for low sample counts. These smokes are functional evidence.

The [I05 reproduction guide](../../experiments/README.md#reproduce-the-archived-results) regenerates the tables and plots from the archives and writes per-run CSV and JSON.
