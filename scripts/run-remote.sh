#!/bin/bash
source $(dirname $0)/../../util/helpers.sh

# We don't need the node list. We only need autorun_logdir.
autorun_gen_nodes=0
overwrite_nodemap=0
source $(dirname $0)/autorun.sh

# Set libhrd parms
if [[ $autorun_cluster == "CIB" ]]
then
	export HRD_REGISTRY_IP="10.113.1.47"
else
	export HRD_REGISTRY_IP="128.110.96.96"
fi

export MLX5_SHUT_UP_BF=0
export MLX5_SINGLE_THREADED=1
export MLX4_SINGLE_THREADED=1
export MLX_QP_ALLOC_TYPE="HUGE"
export MLX_CQ_ALLOC_TYPE="HUGE"

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
	echo "Usage: ./run-servers.sh <machine_number>"
	exit
fi

# With link-time optimization, main exe does not get correct permissions
chmod +x main
drop_shm

# $autorun_logdir must be created at each node (run-all.sh cannot do this)
rm -rf $autorun_logdir
mkdir $autorun_logdir

# The 0th server hosts the QP registry
if [ "$1" -eq 0 ]; then
	echo "Resetting QP registry"
	sudo killall memcached
	memcached -l 0.0.0.0 1>/dev/null 2>/dev/null &
	sleep 1
fi

sudo LD_LIBRARY_PATH=/usr/local/lib/ -E \
	numactl --membind=0 --cpunodebind=0 ./main --machine-id $1 \
		1>$autorun_logdir/out-machine-$1 2>$autorun_logdir/err-machine-$1 &

sleep 10000000
