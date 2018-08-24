#!/usr/bin/env bash
#
# Run all tests one by one in separate processes. This is needed because it is
# difficult to repeatedly cleanup and reinitialize DPDK in one process, and
# gtest does not support running test cases in separate processes.
#
# Run this script from the eRPC/ folder

source $(dirname $0)/utils.sh

exe_list=`ls build | grep test | tr '\n' ' '`
#exe_list=large_msg_test
blue "run-dpdk-tests: Found test executables:"
echo "$exe_list"

for exe in $exe_list; do
  blue "run-dpdk-tests: Running test executable $exe"

  # gtest_list_tests lists the tests as follows:
  # TestCase1.
  #   TestName1
  #   TestName2
  # TestCase2.
  #   TestName3
  # ...
  # For gtest_filter, we need names like TestCase1.TestName1.
  test_list=`./build/$exe --gtest_list_tests`

  for word in $test_list; do
    if [[ $word = *"." ]]; then
      cur_test_name=$word
    else
      echo "run-dpdk-tests: Running test $cur_test_name$word"
      sudo ./build/$exe --gtest_filter=$cur_test_name$word
    fi
  done
done
