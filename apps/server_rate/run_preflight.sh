#!/usr/bin/env bash
# Run from any directory as the regular user, with the DPDK daemon already up.
set -euo pipefail
cd "$(dirname "$0")/../.."
mode=${1:-}
case "$mode" in server|client|sweep) ;; *)
  echo 'Usage: bash apps/server_rate/run_preflight.sh server|client [window: 1..32]|sweep' >&2
  exit 1;;
esac
if ! pgrep -f '(^|/|[[:space:]])erpc_dpdk_daemon([[:space:]]|$)' >/dev/null; then
  echo 'Start the two-port eRPC DPDK daemon first; see apps/server_rate/README.md.' >&2
  exit 1
fi
if pgrep -x latency >/dev/null || pgrep -x pingpong >/dev/null; then
  echo 'Stop latency/pingpong before running this benchmark.' >&2
  exit 1
fi
[[ -x build/server_rate ]] || { echo 'Build server_rate first; see README.md.' >&2; exit 1; }
# Explicit preflight settings; apps/server_rate/config is deliberately unchanged.
common=(--single_host_preflight=true --num_server_threads=1 --num_client_threads=1
        --num_processes=2 --numa_node=0 --numa_1_ports= --req_size=64 --resp_size=8
        --sm_verbose=0 --test_ms=30000 --preflight_warmup_ms=5000)
if [[ "$mode" == server ]]; then
  exec sudo -E ./build/server_rate "${common[@]}" --process_id=0 --numa_0_ports=0
fi
window=${2:-1}
[[ "$window" =~ ^([1-9]|[12][0-9]|3[0-2])$ ]] || {
  echo 'Window must be between 1 and 32.' >&2; exit 1;
}
windows=("$window")
if [[ "$mode" == sweep ]]; then windows=(1 2 4 8 16 32); fi
log="server_rate_preflight_$(date +%Y%m%d_%H%M%S)_$$.txt"
sudo -v
for window in "${windows[@]}"; do
  printf '\n=== request=64 response=8 window=%s ===\n' "$window" | tee -a "$log"
  sudo -E ./build/server_rate "${common[@]}" --process_id=1 --numa_0_ports=1 \
    --window_size="$window" --histogram_file="${log%.txt}_w${window}.hist.csv" 2>&1 | tee -a "$log"
  [[ -s "${log%.txt}_w${window}.hist.csv" ]] || { echo "Missing histogram (interrupted run?)" >&2; exit 1; }
done
printf 'Saved %s/%s\n' "$PWD" "$log"
