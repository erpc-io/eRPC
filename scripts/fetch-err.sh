#!/bin/bash
# Fetch and print the error and output of each process

source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Temporary directory for storing scp-ed files
tmpdir="/tmp/${autorun_app}_errors"
rm -rf $tmpdir
mkdir $tmpdir

echo "fetch-err: Fetching out and err files to $tmpdir"
process_idx=0
while [ $process_idx -lt $autorun_num_processes ]; do
  name=${autorun_name_list[$process_idx]}
  out_file=${autorun_out_prefix}-${process_idx}
  err_file=${autorun_err_prefix}-${process_idx}

  scp -oStrictHostKeyChecking=no \
    $name:$out_file $tmpdir/out-$process_idx 1>/dev/null 2>/dev/null &

  scp -oStrictHostKeyChecking=no \
    $name:$err_file $tmpdir/err-$process_idx 1>/dev/null 2>/dev/null &
  ((process_idx+=1))
done

wait

echo "fetch-err: Finished fetching files to $tmpdir/out-X and $tmpdir/err-X"
echo "fetch-err: grepping for exceptions"
grep -r what /tmp/${autorun_app}_errors/
