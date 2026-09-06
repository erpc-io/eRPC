# Standalone DPDK ping-pong latency baseline

This test measures software-observed round-trip latency through the Solarflare
card and DAC without eRPC. One primary process runs a polling responder on CPU 0
(port 0) and a polling client on CPU 1 (port 1). Both workers verify their CPU
mask after DPDK initialization. Only one request is outstanding.

## Run from the eRPC repository root

1. Stop the eRPC client, server, and daemon, in that order. This benchmark owns
   the ports itself; do not run other DPDK applications on them simultaneously.
2. Keep the DAC connected between ports 0 and 1.
3. Build as your regular user (no sudo needed):

   ```bash
   bash dpdk_pingpong/build.sh
   ```

4. Run the 64-byte request baseline:

   ```bash
   bash dpdk_pingpong/run.sh 64
   ```

The script uses sudo for the executable and saves all output to a timestamped
`dpdk_pingpong/pingpong_64_*.txt` file. Default timing is 5 seconds of warmup,
then 30 seconds of measurement. The program exits and closes both ports when
finished. For a short connectivity check first:

```bash
WARMUP_SECONDS=1 MEASURE_SECONDS=3 bash dpdk_pingpong/run.sh 64
```

Build dependencies: C/C++ compiler, pkg-config, and installed DPDK development
files. The build script uses the existing HdrHistogram source in this repository.
It tries pkg-config first, then `$HOME/dpdk-21.11/install/lib64/pkgconfig`.
For a different install, use `DPDK_PREFIX=/path/to/install bash dpdk_pingpong/build.sh`.
The binary embeds the discovered DPDK library directory as an rpath.

## What is measured

The client timestamps before allocating/copying/submitting its request. The
responder receives and frees the RX mbuf, allocates/copies a reply, and sends it.
The client verifies the reply sequence and frees its RX mbuf before taking the
end timestamp. Thus this is full round-trip latency including userspace packet
processing, not one-way latency or a measurement of NIC hardware alone.

The request length is `56 + request_bytes`; the reply is `56 + 8 = 64` bytes,
excluding Ethernet FCS, preamble, and inter-frame gap. The 56-byte total header
matches `sizeof(erpc::pkthdr_t)` in this DPDK checkout. The test uses valid
Ethernet/IPv4/UDP headers and substitutes a marker/sequence for eRPC metadata.
It does not communicate with an eRPC server.

A 64-byte request therefore sends a **120-byte Ethernet frame**, excluding FCS.
Frames are copied into fresh TX mbufs, like the current eRPC DPDK transmit path.
RX uses the original PMD-returned mbuf pointers, including with Solarflare RX
prefixes. Both ports are started before checking links; hardware loopback is off.

Each printed row is cumulative since warmup ended:

```text
request_bytes median_us p99_us p99.9_us p99.99_us max_us samples elapsed_s
```

Use the last row for comparison. Histogram precision matches the eRPC app's
0.1 µs recording scale and two significant digits. Final NIC counters include
warmup traffic; inspect missed packets, errors, and RX allocation failures.
A missing response or blocked TX for one second aborts the run with an error.
There is no retransmission and no silent exclusion of timed-out requests.

## Limits of the comparison

- Same default CPU IDs, DAC topology, frame lengths, RX/TX ring depths
  (4096/128), 8191 mbufs per port, zero mempool cache, and Solarflare queue quirks.
- **One queue per port with RSS disabled**, versus eRPC's 16 configured queues.
- **One primary process with two polling threads**, versus eRPC's daemon and
  secondary application processes. DPDK service-thread placement can differ.
- Includes its own minimal sequence checking and uses ordered TSC reads;
  eRPC's timestamp boundaries and event loop differ.
- Does not implement sessions, RPC dispatch, congestion control, or reliability.

These differences make this a diagnostic baseline, not an exact subtraction of
"eRPC overhead". In particular, subtracting p99 values does not isolate a p99
software cost. Compare repeated medians first, then investigate the tail.

If this baseline is close to eRPC's approximately 8 µs, investigate the shared
NIC/driver/platform path. If it is substantially lower, investigate eRPC's
additional processing and configuration. Neither outcome alone proves which
individual component is responsible.

## Other sizes and placement

```bash
bash dpdk_pingpong/run.sh 128
bash dpdk_pingpong/run.sh 256
bash dpdk_pingpong/run.sh 512
bash dpdk_pingpong/run.sh 1024
```

Overrides: `SERVER_CPU`, `CLIENT_CPU`, `SERVER_PORT`, `CLIENT_PORT`,
`WARMUP_SECONDS`, and `MEASURE_SECONDS`. For example:

```bash
SERVER_CPU=2 CLIENT_CPU=3 bash dpdk_pingpong/run.sh 64
```

For comparison, use the same placement on both benchmarks. The current eRPC
latency defaults are CPUs 0 and 1. After this standalone test exits, restart
the eRPC daemon and server before rerunning eRPC.

## Validation performed

Built against locally installed DPDK 21.11.0. The build also runs tests that
require no NIC or root privileges: frame lengths, IPv4 checksum, sequence
extraction with varying RX offsets, truncated/wrong-destination packets, and
argument rejection. Actual two-port hardware RTT remains to be measured.
