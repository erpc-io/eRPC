#!/usr/bin/env bash
# Utilities for other scripts

# Echo in blue color
function blue() {
	es=`tput setaf 4`
	ee=`tput sgr0`
	echo "${es}$1${ee}"
}

# Drop all SHM
function drop_shm() {
	for i in $(ipcs -m | awk '{ print $1; }'); do
		if [[ $i =~ 0x.* ]]; then
			sudo ipcrm -M $i 2>/dev/null
		fi
	done
}

# Check if a file exists. If it does not, exit.
function assert_file_exists() {
  if [ ! -f $1 ]; then
    echo "utils: File $1 not found! Exiting."
    exit 0
  fi
}
