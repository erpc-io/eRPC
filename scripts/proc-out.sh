#!/bin/bash
# Process the output of each process in /tmp/autorun_stat_file
#
# Each stat file begins with the name of the stats like so:
# stat_name_1 stat_name_2 ... stat_name_N
# Then, there are lines containing the values of the stats, like so:
# val_1 val_2 ... val_N
#
# The names and values must be space-separated. Names should be a single word.
#
# This requires the awk scripts from anujkaliaiitd/systemish

source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Temporary directory for storing scp-ed files
tmpdir="/tmp/${autorun_app}_proc"
rm -rf $tmpdir
mkdir $tmpdir

process_idx=0
while [ $process_idx -lt $autorun_num_processes ]; do
  node_name=${autorun_name_list[$process_idx]}
  stat_file=${autorun_app}_stats_${process_idx}

	echo "proc-out: Fetching $stat_file from $node_name"
  scp -oStrictHostKeyChecking=no \
    $node_name:/tmp/$stat_file $tmpdir/$stat_file 1>/dev/null 2>/dev/null &
  ((process_idx+=1))
done

wait
echo "proc-out: Finished fetching files."

header_line=`cat $tmpdir/* | head -1`
num_columns=`echo $header_line | wc -w`
blue "proc-out: Header = [$header_line]"
echo "proc-out: Detected $num_columns columns"

# Sum of per-file average, and sum of (per-file average)^2 for each column.
# Averaging all rows across all files is incorrect.
for ((col_idx = 1; col_idx <= $num_columns; col_idx++)); do
  col_name[$col_idx]=`echo $header_line | col.awk $col_idx`
done

# Process the fetched stats files
processed_files="0"
ignored_files="0"

# The per-file column averages for column i are stored in file "col_avg_i" 
rm -f col_avg*

for filename in `ls $tmpdir/* | sort -t '-' -k 2 -g`; do
  filename_short=`echo $filename | rev | cut -d'/' -f 1 | rev`  # foo/bar/xxx
  echo "proc-out: Processing file $filename_short"

  # Ignore files <= 8 lines. This takes care of empty files.
  lines_in_file=`cat $filename | wc -l`
  if [ $lines_in_file -le 8 ]; then
    blue "proc-out: Ignoring $filename_short. $lines_in_file lines, 8 needed."
    ((ignored_files+=1))
    continue;
  fi

  # Strip out first 4 and last 4 lines, and save rest for processing to tmp file
  awk -v nr="$(wc -l < $filename)" 'NR > 4 && NR < (nr - 4)' $filename > proc_out_tmp

  file_avg_str=""  # Average of each column for this file
  for ((col_idx = 1; col_idx <= $num_columns; col_idx++)); do
    col_avg_file=`cat proc_out_tmp | col.awk $col_idx | avg.awk`
    echo $col_avg_file >> col_avg_$col_idx
    file_avg_str=`echo $file_avg_str ${col_name[$col_idx]}:$col_avg_file, `
  done

  echo "proc-out: Column averages for $filename_short = $file_avg_str"
  ((processed_files+=1))
done

for ((col_idx = 1; col_idx <= $num_columns; col_idx++)); do
  col_avg_all_files=`cat col_avg_$col_idx | avg.awk`
  col_stddev_all_files=`cat col_avg_$col_idx | stddev.awk`
  blue "proc-out: Final column ${col_name[$col_idx]} average = $col_avg_all_files, stddev $col_stddev_all_files"
done

blue "proc-out: Processed files = $processed_files, ignored files = $ignored_files"
rm -f proc_out_tmp
rm -f col_avg*
