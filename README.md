eRPC is a fast and general-purpose RPC library for kernel-bypass fabrics.
Some highlights:
 * Small RPC rate: 10 million RPCs/second per CPU core
 * Low latency: 2.5 microseconds round-trip RPC latency
 * Large RPC bandwidth: 40 Gbps transfer per CPU core
 * Scalability: 12000 or more RPC sessions per server
 * End-to-end congestion control
 * Nested RPCs, and long-running background RPCs
 * Support for UDP (PFC is recommended), InfiniBand, and RoCE
 * A port of [Raft](https://github.com/willemt/raft) as an example, with
   5.3 microseconds of client-measured latency

## Requirements
 * A C++11 compiler, specified in `CMakeLists.txt`. clang and gcc have been
   tested.
 * See `scripts/packages.sh` for a list of required software packages.
 * Supported NICs:
   * UDP over Ethernet mode: ConnectX-4 or newer Mellanox Ethernet NICs.
     Non-Mellanox NICs are not supported. ConnectX-3 and older Mellanox NICs are
     supported in eRPC's RoCE mode. PRs for unsupported NICs are welcome.
   * It's best to use drivers from Mellanox OFED. Mellanox drivers specially
     optimized for eRPC are available in the `drivers` directory, but they are
     primarily for expert use.
   * Upstream drivers work as well. This requires installing the `ibverbs` and
     `mlx4` userspace packages, and enabling the `mlx4_ib` and `ib_uverbs`
     kernel drivers. On Ubuntu, the incantation is:
      * `apt install libmlx4-dev libibverbs-dev`
      * `modprobe mlx4_ib ib_uverbs`
   * For Connect-IB and newer NICs, use `mlx4` by `mlx5`.
 * Unlimited SHM limits, and at least 2048 huge pages on every NUMA node.

## eRPC configuration
 * `src/tweakme.h` defines parameters that govern eRPC's behavior.
   * `CTransport` defines which fabric transport eRPC is compiled for. It can
      be set to `IBTransport` for InfiniBand, or `RawTransport` for Mellanox's
      "Raw" Ethernet transport. `kHeadroom` must be set accordingly.
   * Parameters with the `kCc` prefix govern eRPC's congestion control behavior.
 * Since compilation is slow, the CMake build compiles only one application,
   defined by the contents of `scripts/autorun_app_file`. This file should
   contain the name of a directory in `apps` (e.g., `small_rpc_tput`).
 * The URIs of eRPC processes in the cluster are specified in
   `scripts/autorun_process_file`. Each line in this file must be
   `<hostname> <management udp port> <numa_node>`. We allow running one eRPC
   process per NUMA node. See `scripts/gen_autorun_process_file.sh` for how
   to generate this file.
 * Each application directory in `apps` (except `hello`) contains a config file
   that must contain the flags defined in `apps/apps_common.h`. In addition, it
   may contain any application-specific flags.

## eRPC quickstart
 * Build and run the test suite: `cmake . -DPERF=OFF; make -j; sudo ctest`.
 * Generate the documentation: `doxygen`
 * Running the hello world application in `apps/hello`:
   * Compile the eRPC library using CMake.
   * This application requires two machines. Set `kServerHostname` and
     `kClientHostname` to the IP addresses of your machines.
   * Build the application using `make`.
   * Run `./server` at the server, and `./client` at the client.

## Running the applications
 * We provide a suite of benchmarks in the `apps` directory.
 * To build an application, change the contents of `scripts/autorun_app_file`
   to one of the available applications. Then generate a Makefile using
   `cmake . -DPERF=ON/OFF`
 * The number of processes used in an eRPC app is passed as a command line flag
   (see `num_processes` in `apps/apps_common.h`). `scripts/do.sh` is used to
   run apps manually.
   * With single-CPU machines: `num_processes` machines are needed.
     Run `scripts/do.sh <i> 0` on machine `i` in `{0, ..., num_processes - 1}`.
   * With dual-CPU machines: `num_machines = ceil(num_processes / 2)` machines
     are needed. Run `scripts/do.sh <i> <i % 2>` on machine i in
     `{0, ..., num_machines - 1}`.
 * To automatically run an app, use `scripts/run-all.sh`. Application
   statistics generated in a run can be analysed using `scripts/proc-out.sh`.

## Getting help
 * GitHub issues are preferred over email.

## Contact
Anuj Kalia (akalia@cs.cmu.edu)

## License
		Copyright 2018, Carnegie Mellon University

        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

            http://www.apache.org/licenses/LICENSE-2.0

        Unless required by applicable law or agreed to in writing, software
        distributed under the License is distributed on an "AS IS" BASIS,
        WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
        See the License for the specific language governing permissions and
        limitations under the License.

