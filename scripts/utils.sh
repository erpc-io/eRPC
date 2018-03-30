#!/usr/bin/env bash
# Utilities for other scripts

# Echo in blue color. Actually, is yellow.
function blue() {
	es=`tput setaf 3`
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

# Check if an environment variable is set. If it is not, exit.
function check_env() {
  if [ -z "$1" ]; then
    echo "utils: Environment variable $1 not set. Exiting."
    exit
  fi
}

# Check if a file ($1) exists. If it does not, exit.
function assert_file_exists() {
  if [ ! -f $1 ]; then
    echo "utils: File $1 not found! Exiting."
    exit 0
  fi
}

# Check if there are at least 512 hugepages. If not, exit.
function hugepages_or_exit() {
  num_hugepages=`cat /sys/devices/system/node/*/meminfo | \
    grep "Node 0 HugePages_Total" | \
    cut -d":" -f 2 | \
    sed -e 's/ *//'`

  if [ $num_hugepages -lt 512 ]; then
    echo "Insufficient hugepages"
  fi
}
