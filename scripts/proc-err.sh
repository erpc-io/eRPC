#!/bin/bash
# Print the error and output of each process

source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Temporary directory for storing scp-ed files
tmpdir="/tmp/${autorun_app}_errors"
rm -rf $tmpdir
mkdir $tmpdir

echo "proc-err: Fetching out and err files to $tmpdir"
process_idx=0
while [ $process_idx -lt $autorun_num_processes ]; do
  name=${autorun_name_list[$process_idx]}
  out_file=${autorun_out_prefix}-${process_idx}
  err_file=${autorun_err_prefix}-${process_idx}

  scp -oStrictHostKeyChecking=no \
    $name:$out_file $tmpdir/out-$process_idx 1>/dev/null 2>/dev/null &

  scp -oStrictHostKeyChecking=no \
    $name:$out_file $tmpdir/err-$process_idx 1>/dev/null 2>/dev/null &
  ((process_idx+=1))
done

wait
echo "proc-err: Finished fetching files."

for ((process_idx = 0; process_idx < $autorun_num_processes; process_idx++)); do
  echo ""
  echo "======================================================================="
  echo "Process $process_idx out:"
  cat $tmpdir/out-$process_idx

  echo "Process $process_idx err:"
  cat $tmpdir/err-$process_idx
done
