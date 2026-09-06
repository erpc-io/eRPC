#!/usr/bin/env bash
# Client window only: keep the two-port daemon and latency server running.
set -euo pipefail
cd "$(dirname "$0")/.."
trials=${1:-3}
[[ "$trials" =~ ^[1-9][0-9]*$ ]] || { echo 'Usage: bash scripts/collect_solarflare_histograms.sh [trials>=1]' >&2; exit 1; }
config_args=()
while read -r -a line; do config_args+=("${line[@]}"); done < apps/latency/config
run_tag="$(date +%Y%m%d_%H%M%S)_$$"
sudo -v
for ((trial=1; trial<=trials; trial++)); do
  for size in 64 128 256 512 1024; do
    prefix="solarflare_latency_${run_tag}_trial${trial}_${size}B"
    sudo -E ./build/latency "${config_args[@]}" \
      --process_id=1 --numa_node=0 --numa_0_ports=1 \
      --resp_size=8 --req_size="$size" --warmup_ms=5000 --test_ms=30000 \
      --histogram_file="$prefix.hist.csv" 2>&1 | tee "$prefix.txt"
    [[ -s "$prefix.hist.csv" ]] || { echo 'Missing histogram (interrupted run?)' >&2; exit 1; }
  done
done
