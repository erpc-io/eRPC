#!/usr/bin/env bash
source $(dirname $0)/utils.sh
if [ "$#" -ne 1 ]; then
  echo "Illegal number of parameters"
	echo "Usage: ./xl170-topo.sh <number of nodes to look up>"
	exit
fi

bad_nodes="hp186 hp187"

# Create a map from node hostnames to switch IDs
for ((i = 1; i <= $1; i++)); do
  (
  hostname="akalianode-$i.RDMA.ron-PG0.utah.cloudlab.us"

  # Get the HP node ID (example hp012)
	hpnode_id=`ssh -oStrictHostKeyChecking=no $hostname \
    "hostname -A | cut -d '.' -f 1"`

  # Ignore if node is bad
  if [[ $bad_nodes == *"$hpnode_id"*  ]]; then
    blue "Ignoring bad node $hpnode_id ($hostname)"
  else
    hpnode_id=`echo $hpnode_id | sed 's/hp0*//g'` # Trim leading hp and zeros
    switch_id=$(( ($hpnode_id - 1) / 40 ))

    echo "$hostname $switch_id" >> temp
  fi

  ) &
done
wait

# Here, temp contains:
# <hostname> <switch_id>
echo ""
for ((switch_i = 0; switch_i < 5; switch_i++)); do
  count=`cat temp | grep " $switch_i" | wc -l`
  blue "Under switch $switch_i ($count nodes):"
  cat temp | grep " $switch_i" | cut -d' ' -f 1
done

rm temp
