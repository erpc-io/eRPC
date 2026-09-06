#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Do not allow simultaneous ownership of the ports by the eRPC daemon/apps.
if pgrep -f '(^|/|[[:space:]])erpc_dpdk_daemon([[:space:]]|$)' >/dev/null || pgrep -x latency >/dev/null || pgrep -x server_rate >/dev/null; then
  echo 'Stop the eRPC client, server, and daemon before this standalone test.' >&2
  exit 1
fi
size=${1:-64}
[[ "$size" =~ ^[0-9]+$ ]] && (( size >= 64 && size <= 1024 )) || {
  echo 'Usage: bash dpdk_pingpong/run.sh [request_bytes: 64..1024]' >&2; exit 1;
}
[[ -x build/pingpong ]] || bash build.sh
log="pingpong_${size}_$(date +%Y%m%d_%H%M%S)_$$.txt"
# Defaults mirror the eRPC workers. EAL initializes on client CPU, then both
# polling workers explicitly pin and verify their masks after initialization.
sudo -E ./build/pingpong -l "${CLIENT_CPU:-1}" -n 6 -m 1024 \
  --proc-type=primary --log-level=4 -- \
  "$size" "${WARMUP_SECONDS:-5}" "${MEASURE_SECONDS:-30}" \
  "${SERVER_PORT:-0}" "${CLIENT_PORT:-1}" \
  "${SERVER_CPU:-0}" "${CLIENT_CPU:-1}" "${log%.txt}.hist.csv" 2>&1 | tee "$log"
printf 'Saved %s/%s\n' "$PWD" "$log"
