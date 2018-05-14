#!/usr/bin/env bash
source $(dirname $0)/utils.sh

# Sweep over Timely's congestion control params
sweep_dir="/tmp/timely_sweep"
rm -rf $sweep_dir

timely_sweep_params_h="src/cc/timely_sweep_params.h"

# 6 machines on NetApp, so increment NUM_WORKERS by 6
for kPatched in false true; do
	for kEwmaAlpha in 0.02 0.08 0.1 0.4 0.8; do
		for kBeta in 0.01 0.04 0.1 0.4 0.8; do
			rm $timely_sweep_params_h
      touch $timely_sweep_params_h

      echo "static constexpr bool kPatched = $kPatched;" >> $timely_sweep_params_h
      echo "static constexpr double kEwmaAlpha = $kEwmaAlpha;" >> $timely_sweep_params_h
      echo "static constexpr double kBeta = $kBeta;" >> $timely_sweep_params_h
      
      echo "Building for kPatched = $kPatched, kEwmaAlpha = $kEwmaAlpha, kBeta = $kBeta"
      make clean
			make -j 1>/dev/null 2>/dev/null

      echo "Running"
			./scripts/run-all.sh 1>/dev/null 2>/dev/null

      echo "Fetching output"
      ./scripts/proc-out.sh | grep "Final"

      echo "Cleaning up"
      ./scripts/kill-all.sh 1>/dev/null 2>/dev/null

      echo ""
		done
	done
done
