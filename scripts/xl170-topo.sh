#!/usr/bin/env bash
source $(dirname $0)/autorun_parse.sh

switch_map[0]=""  # switch_map[i] will contain the list of nodes under switch i

tmpfile="xl170topo.txt"
rm -rf $tmpfile
touch $tmpfile

# Fetch xxx from hpxxx for each node in parallel
for ((i = 0; i < $autorun_num_processes; i++)); do
  (
	hpnode=`ssh -oStrictHostKeyChecking=no ${autorun_name_list[$i]} \
    "hostname -A | cut -d '.' -f 1 | sed 's/hp//g'"`
  echo "$(( $i + 1 )) $hpnode" >> $tmpfile
  ) &
done
wait

while read line
do
  akalianode_id=`echo $line | cut -d' ' -f 1`
  hpnode_id=`echo $line | cut -d' ' -f 2 | sed 's/^0*//'`  # Trim leading zeros
  switch_id=$(( ($hpnode_id - 1) / 40 ))
	switch_map[$switch_id]=$akalianode_id" "${switch_map[$switch_id]}

  echo "Node $akalianode_id under switch $switch_id"
done < $tmpfile


for ((switch_i = 0; switch_i < 5; switch_i++)); do
	sorted_nodes=`echo ${switch_map[$switch_i]} | xargs -n 1 | sort -g | tr '\n' ' '`
	echo Switch $switch_i: $sorted_nodes
done

rm $tmpfile
