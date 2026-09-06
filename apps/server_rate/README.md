# Server rate: Solarflare single-host preflight

The existing server_rate app supports multiple outstanding RPCs per client via
`--window_size`. This preflight compares 1, 2, 4, 8, 16, and 32 outstanding RPCs
with one client worker and one server worker on the two-port DAC setup.

## Explicit gate and Mellanox behavior

`--single_host_preflight` defaults to **false**. When enabled it:

- Requires process IDs 0/1 and exactly one server/client worker.
- Pins the server to NUMA-local CPU index 0 and client to index 1. On the current
  Ryzen these are distinct physical cores (Linux CPUs 0 and 1).
- Adds a separate warmup (`--preflight_warmup_ms`, default 5000) before measurement.
- Reports decimal **Mrps = completed requests / seconds / 1,000,000**.
- Records cumulative HDR latency percentiles with a 0.1 µs recording unit, two
  significant digits, and a 100-second range. Requests crossing the warmup
  boundary are counted as completions but excluded from latency statistics.
- Fails on an interval with no completed requests, and emits a final summary.

Port selection still uses the existing `--numa_0_ports`/`--numa_1_ports` flags.
No new Solarflare checks or NIC settings were added to the app or transport.
The stock `config` is unchanged. With the gate off, thread placement stays at
NUMA-local index `thread_id`, and the legacy interval histogram/output and
binary-million rate divisor remain in use. The affinity race fix applies in
both modes: each worker pins itself after transport initialization.

On separate Mellanox hosts, omit the preflight flag (or set it false), choose
the correct port/NUMA settings on each host, and configure thread counts for
those machines. The local launch script below is only for the current two-port,
single-host topology; do not use it unchanged for a multi-host test.

## Build without changing the selected latency app

The optional CMake target is disabled by default. For the current in-source
build, run from the repository root:

```bash
cmake -S . -B . -DERPC_BUILD_SERVER_RATE=ON
cmake --build . --target server_rate -j4
```

This preserves `scripts/autorun_app_file` and `build/latency`. On another build,
use its own build directory with the project's normal transport configuration.

## Run on the current machine

Stop existing latency or standalone pingpong processes. The DPDK daemon must
manage both ports, with the DAC connected and hardware loopback disabled.
If it is not already running, start it in terminal 1:

```bash
sudo -E ./build/erpc_dpdk_daemon \
  --phy_port=0 --peer_phy_port=1 --numa_node=0 --loopback=false
```

Wait for all managed links to be up. In terminal 2 start the server:

```bash
bash apps/server_rate/run_preflight.sh server
```

Confirm it attaches as a secondary process and reports CPU 0. In terminal 3,
first run a window of one:

```bash
bash apps/server_rate/run_preflight.sh client 1
```

The client should report CPU 1 and complete its 5-second warmup plus 30-second
measurement. Then run the full concurrency sweep (about 210 seconds plus startup):

```bash
bash apps/server_rate/run_preflight.sh sweep
```

The server and daemon remain running between clients. The script saves output
to a unique `server_rate_preflight_TIMESTAMP_PID.txt` in the repository root.
The existing `scripts/autorun_process_file` must identify the two processes on
this host with distinct session-management UDP ports.

## Read the results

Each completed client run prints:

```text
PREFLIGHT_SUMMARY req_bytes=64 resp_bytes=8 window=... completed=... elapsed_s=... mrps=... median_us=... p99_us=... p99.9_us=... max_us=... latency_samples=...
```

Record one summary per window. Plot window size versus Mrps, and Mrps versus
p99 latency. Do not average per-second percentiles: summary percentiles come
from the full post-warmup histogram. Summary throughput uses wall time including
reporting between intervals. The server also prints its live rate; use the
client summary for completed round trips.

With window=1, expect a rate constrained by RTT. Larger windows can overlap
requests, increase batching, and raise rate until CPU or network capacity is
reached. More throughput can also increase tail latency. This is a closed-loop
concurrency sweep, not an independently controlled offered-load experiment.

The test is instrumented: per-RPC timing/histogram work consumes CPU. It retains
server_rate's ChronoTimer timing, whereas the latency app uses TSC timing, so
small differences between the two apps are not necessarily transport changes.
This is a single-host preflight, not a reproduction of the paper's multi-node
message-rate experiment.

Build and argument checks can be performed without hardware. Sustained results
and a Mellanox run still need hardware validation. Stop the server before the
daemon when finished.
