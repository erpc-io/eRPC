#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Override DPDK_PREFIX if DPDK is installed elsewhere.
if [[ -n "${DPDK_PREFIX:-}" ]]; then
  export PKG_CONFIG_PATH="$DPDK_PREFIX/lib64/pkgconfig:$DPDK_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
elif ! pkg-config --exists libdpdk; then
  export PKG_CONFIG_PATH="$HOME/dpdk-21.11/install/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
fi
pkg-config --exists libdpdk
mkdir -p build
cc -O2 -I../third_party/HdrHistogram_c/src \
  -c ../third_party/HdrHistogram_c/src/hdr_histogram.c -o build/hdr_histogram.o
read -r -a cflags <<< "$(pkg-config --cflags libdpdk)"
read -r -a libs <<< "$(pkg-config --libs libdpdk)"
c++ -std=c++14 -O2 -g -Wall -Wextra -pthread "${cflags[@]}" \
  -I../third_party/HdrHistogram_c/src main.cc build/hdr_histogram.o \
  "${libs[@]}" -Wl,-rpath,"$(pkg-config --variable=libdir libdpdk)" -lm -o build/pingpong
printf 'Built dpdk_pingpong/build/pingpong with DPDK %s\n' "$(pkg-config --modversion libdpdk)"
c++ -std=c++14 -O2 -g -Wall -Wextra -pthread "${cflags[@]}" \
  -I../third_party/HdrHistogram_c/src test.cc build/hdr_histogram.o \
  "${libs[@]}" -Wl,-rpath,"$(pkg-config --variable=libdir libdpdk)" -lm -o build/test
./build/test
