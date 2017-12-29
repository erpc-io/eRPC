#!/bin/bash
# Process the output of each process in /tmp/autorun_stat_file
#
# Each stat file begins with the name of the stats like so:
# stat_name_1 stat_name_2 ... stat_name_N
# Then, there are lines containing the values of the stats, like so:
# val_1 val_2 ... val_N
#
# The names and values must be space-separated. Names should be a single word.
# The bash maps in this scripts are indexed from one !!!!

source $(dirname $0)/utils.sh
source $(dirname $0)/autorun_parse.sh

# Temporary directory for storing scp-ed files
tmpdir="/tmp/${autorun_app}_proc"
rm -rf $tmpdir
mkdir $tmpdir

for i in `seq 1 $autorun_num_processes`; do
  name=${autorun_name_list[$i]}
  stat_file="$autorun_stat_prefix-$i"

	echo "proc-out: Fetching $stat_file from $name to $tmpdir/$stat_file"
  scp $name:$stat_file $tmpdir/$stat_file 1>/dev/null 2>/dev/null &
done
wait
echo "proc-out: Finished fetching files."

header_line=`cat $tmpdir/* | head -1`
blue "proc-out: Header = [$header_line]"

# Create a map of column names in the stats file, indexed from 1
col_name[1]=""
col_index="1"
for name in $header_line; do
  col_name[$col_index]=$name
  echo "Column $col_index = ${col_name[$col_index]}"
  ((col_index+=1))
done

num_columns=`echo $header_line | wc -w`
echo "proc-out: Detected $num_columns columns"

# Sum of per-file average for each column. Averaging rows is incorrect.
col_sum_of_avgs[1]=""
for col in `seq 1 $num_columns`; do
  col_sum_of_avgs[$col]="0.0"
done

# Process the fetched stats files
processed_files="0"
ignored_files="0"
for filename in `ls $tmpdir/* | sort -t '-' -k 2 -g`; do
  echo "proc-out: Processing file $filename."

  # Ignore files with less than 12 lines. This takes care of empty files.
  lines_in_file=`cat $filename | wc -l`
  if [ $lines_in_file -le 12 ]; then
    blue "proc-out: Ignoring $filename. Too short ($lines_in_file lines), 12 required."
    ((ignored_files+=1))
    continue;
  fi

  # Strip out first 6 and last 6 lines, and save rest for processing to tmp file
  awk -v nr="$(wc -l < $filename)" 'NR > 6 && NR < (nr - 6)' $filename > proc_out_tmp
  remaining_rows=`cat proc_out_tmp | wc -l`

  file_avg_str=""  # Average of each column for this file
  for col in `seq 1 $num_columns`; do
    file_avg=`awk -v col=$col '{ total += $col } END { printf "%.3f", total / NR  }' proc_out_tmp`
    col_sum_of_avgs[$col]=`echo "scale=3; ${col_sum_of_avgs[$col]} + $file_avg" | bc -l`
    file_avg_str=`echo $file_avg_str ${col_name[$col]}:$file_avg, `
  done

  echo "proc-out: Column averages for $filename = $file_avg_str"
  ((processed_files+=1))
done

for col in `seq 1 $num_columns`; do
  col_avg=`echo "scale=3; ${col_sum_of_avgs[$col]} / $processed_files" | bc -l`
  blue "proc-out: Final column ${col_name[$col]} average = $col_avg"
done

blue "proc-out: Processed files = $processed_files, ignored files = $ignored_files"

rm -f proc_out_tmp
