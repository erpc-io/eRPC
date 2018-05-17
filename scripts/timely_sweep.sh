#!/usr/bin/env bash
source $(dirname $0)/utils.sh

function echo_and_log() {
  echo $1
  echo $1 > sweep_out
}

# Sweep over Timely's congestion control params
sweep_dir="/tmp/timely_sweep"
rm -rf $sweep_dir

timely_sweep_params_h="src/cc/timely_sweep_params.h"

for kPatched in false; do
	for kEwmaAlpha in .4 .6; do
    for kBeta in `seq .2 .04 .4`; do
      rm $timely_sweep_params_h
      touch $timely_sweep_params_h

      echo "static constexpr bool kPatched = $kPatched;" >> $timely_sweep_params_h
      echo "static constexpr double kEwmaAlpha = $kEwmaAlpha;" >> $timely_sweep_params_h
      echo "static constexpr double kBeta = $kBeta;" >> $timely_sweep_params_h
      
      echo_and_log "Building for kPatched = $kPatched, kEwmaAlpha = $kEwmaAlpha, kBeta = $kBeta"
      make clean
      make -j 1>/dev/null 2>/dev/null

      echo_and_log "Running"
      ./scripts/run-all.sh 1>/dev/null 2>/dev/null

      echo_and_log "Fetching output"
      ./scripts/proc-out.sh | grep -E "Final column tx_gbps|Final column rtt"

      echo_and_log "Cleaning up"
      ./scripts/kill-all.sh 1>/dev/null 2>/dev/null

      echo_and_log ""
    done
  done
done
