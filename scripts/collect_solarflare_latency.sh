#!/usr/bin/env bash
# Run as the regular user; sudo is used only for the benchmark.
set -euo pipefail
cd "$(dirname "$0")/.."
log="solarflare_latency_$(date +%Y%m%d_%H%M%S)_$$.txt"
if ! pgrep -f '^\./build/erpc_dpdk_daemon([[:space:]]|$)' >/dev/null; then
  echo 'Start the daemon and server using SOLARFLARE_LATENCY.md first.' >&2
  exit 1
fi
sudo -v
# apps/latency/config contains whitespace-separated gflags arguments.
config_args=()
while read -r -a line; do
  config_args+=("${line[@]}")
done < apps/latency/config
printf 'Solarflare fixed-size sweep: 8-byte responses; 5000 ms warmup; 30000 ms measurement per size\n' | tee "$log"
for size in 64 128 256 512 1024; do
  printf '\n=== request_bytes=%s ===\n' "$size" | tee -a "$log"
  sudo -E ./build/latency "${config_args[@]}" \
    --process_id=1 --numa_node=0 --numa_0_ports=1 \
    --resp_size=8 --req_size="$size" --warmup_ms=5000 --test_ms=30000 \
    --histogram_file="${log%.txt}_${size}B.hist.csv" \
    2>&1 | tee -a "$log"
  [[ -s "${log%.txt}_${size}B.hist.csv" ]] || { echo "Missing histogram (interrupted run?)" >&2; exit 1; }
done
printf '\nSaved raw output: %s\n' "$log"
# Prefer the temporary plotting dependencies installed during setup if present.
if [[ -d /tmp/erpc-plot-deps ]]; then
  PYTHONPATH="/tmp/erpc-plot-deps${PYTHONPATH:+:$PYTHONPATH}" \
    MPLCONFIGDIR=/tmp/erpc-mpl python3.11 scripts/plot_solarflare_sizes.py "$log"
else
  python3 scripts/plot_solarflare_sizes.py "$log"
fi
