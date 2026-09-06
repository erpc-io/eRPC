## Historical results — repeated histogram measurements, 2026-09-05

Validated **five eRPC size-sweep trials**, **three concurrency sweeps**, and
**three DPDK baseline trials**: 46 complete measured distributions. Every run
used 5 seconds warmup and 30 seconds measurement, with 8-byte responses.
Histogram totals match the corresponding text logs; all required measurement
intervals completed. The three DPDK runs reported zero missed packets, RX/TX
errors, and RX allocation failures on both ports.

These are Phase 0 single-host Solarflare measurements. DPDK and eRPC have the
configuration differences described above; their RTT gap is not isolated
protocol overhead.

### Repeatability and error bars

Tables show **mean of per-trial statistics [minimum, maximum]**. Error bars show
the same observed between-trial range, not standard deviation or a confidence
interval. Each trial has equal weight. A zero-width median range means the
recorded medians agree at HDR precision; it does not prove zero timing variation.

![Repeatability: baseline, message sizes, throughput](results/solarflare/repeated-20260905/repeatability.png)

| 64-byte test | Trials | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) |
|---|---:|---:|---:|---:|
| DPDK ping-pong | 3 | 7.70 [7.70, 7.70] | 8.50 [8.50, 8.50] | 13.73 [13.60, 13.90] |
| eRPC latency | 5 | 8.00 [8.00, 8.00] | 8.96 [8.80, 9.00] | 16.08 [15.30, 16.60] |

### Request-size sweep: five trials per size

| Request bytes | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) | Total measured samples |
|---:|---:|---:|---:|---:|
| 64 | 8.00 [8.00, 8.00] | 8.96 [8.80, 9.00] | 16.08 [15.30, 16.60] | 18,339,459 |
| 128 | 8.04 [8.00, 8.10] | 8.98 [8.90, 9.10] | 16.06 [15.10, 17.50] | 18,144,629 |
| 256 | 8.26 [8.20, 8.30] | 9.20 [9.10, 9.30] | 17.00 [15.70, 17.50] | 17,706,655 |
| 512 | 8.40 [8.40, 8.40] | 9.36 [9.30, 9.40] | 18.20 [16.10, 19.90] | 17,375,577 |
| 1024 | 9.24 [9.20, 9.30] | 10.54 [10.50, 10.60] | 21.02 [20.20, 22.80] | 15,758,548 |

### Concurrency sweep: three trials per window

| Window | Throughput (Mrps) | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) |
|---:|---:|---:|---:|---:|
| 1 | 0.122 [0.121, 0.122] | 8.00 [8.00, 8.00] | 9.00 [9.00, 9.00] | 15.63 [15.20, 16.50] |
| 2 | 0.237 [0.237, 0.237] | 8.20 [8.20, 8.20] | 9.30 [9.30, 9.30] | 15.57 [15.40, 15.90] |
| 4 | 0.439 [0.439, 0.440] | 8.83 [8.80, 8.90] | 10.70 [10.60, 10.80] | 17.37 [17.00, 17.80] |
| 8 | 0.783 [0.782, 0.785] | 9.90 [9.90, 9.90] | 16.57 [16.50, 16.60] | 18.60 [18.40, 18.80] |
| 16 | 0.777 [0.776, 0.778] | 20.13 [20.10, 20.20] | 28.50 [28.50, 28.50] | 32.43 [32.30, 32.50] |
| 32 | 0.778 [0.777, 0.778] | 40.37 [40.30, 40.50] | 50.03 [49.90, 50.10] | 59.10 [56.70, 60.70] |

![Concurrency latency with trial ranges](results/solarflare/repeated-20260905/concurrency-latency.png)

Increasing concurrency initially raises throughput. Beyond window 8, the rate
changes little while RTT rises substantially. This is consistent with queueing
at a processing limit, but does not identify whether the limit is in the client,
server, PMD, or NIC. These repetitions characterize observed variation; they do
not rule out thermal or operating-system interference.

### Full histograms, CDFs, and tail CCDFs

Every curve below is one trial; colors group identical conditions. No trials
are pooled and no curves are reconstructed from a few percentile points.
The frequency histogram shows the peak; the CDF and CCDF retain the full range,
including rare slow samples. The CCDF uses log probability; distribution plots
also use log latency to keep the full tails visible. Zero CCDF values are
omitted. Far-tail estimates supported by only a few samples are unstable.

![64-byte frequency histograms](results/solarflare/repeated-20260905/baseline-histogram.png)

![DPDK versus eRPC CDF and CCDF](results/solarflare/repeated-20260905/baseline-distributions.png)

![All request sizes: CDF and CCDF](results/solarflare/repeated-20260905/size-distributions.png)

![Windows 1, 8, 32: CDF and CCDF](results/solarflare/repeated-20260905/window-distributions.png)

### Saved data and reproduction

- [Per-trial statistics](results/solarflare/repeated-20260905/per-trial.csv)
- [All 46 histogram CSVs](results/solarflare/repeated-20260905/histograms/)
- [Source filenames and histogram SHA-256 checksums](results/solarflare/repeated-20260905/manifest.json)
- [Compressed source text logs](results/solarflare/repeated-20260905/raw-logs.tar.gz)
- [Analysis and plotting script](scripts/plot_solarflare_repeated.py)

PNG and SVG versions of each plot are in the results directory. Source text
logs from these completed trials are archived together; loose copies are removed
after validation. Histogram CSVs are retained. Older exploratory logs are not
part of this cleanup or this repeated-trial analysis.

Recreate the tables and figures from the preserved dataset without NIC access:

```bash
PYTHONPATH=/tmp/erpc-plot-deps MPLCONFIGDIR=/tmp/erpc-mpl python3.11 scripts/plot_solarflare_repeated.py
```

The script writes `results/solarflare/repeated-20260905/report-section.md` alongside
the figures. This replaces the earlier single-run historical comparison for
current discussion; older reference figures remain in `results/solarflare/`.
