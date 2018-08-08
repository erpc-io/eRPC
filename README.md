eRPC is a fast and general-purpose RPC library for datacenter networks.
We have a [preprint](https://arxiv.org/pdf/1806.00680.pdf) that describes the
system.

Some highlights:
 * Multiple supported networks: UDP (without or with PFC), InfiniBand, and RoCE
 * Performance for small RPCs: ~10 million 32-byte RPCs/second per CPU core
 * Low latency: 2.3 microseconds round-trip RPC latency with UDP over Ethernet
 * High bandwidth for large RPC: 40 Gbps transfer per CPU core for 8 MB RPCs
 * Scalability: 20000 RPC sessions per server
 * End-to-end congestion control that tolerates 100-way incasts
 * Nested RPCs, and long-running background RPCs
 * A port of [Raft](https://github.com/willemt/raft) as an example. Our 3-way
   replication latency is 5.3 microseconds with traditional UDP over Ethernet.

## Software requirements
 * See `scripts/packages.sh` for a list of required software packages.
 * NIC-specific requirements:
   * For Mellanox NICs, install the latest
     [Mellanox OFED](http://www.mellanox.com/page/products_dyn?product_family=26).
   * For DPDK, place the latest DPDK in `${HOME}/dpdk`.
 * At least 512 huge pages on every NUMA node, and unlimited SHM limits.

## eRPC quickstart
 * Build and run the test suite:
   `cmake . -DPERF=OFF -DTRANSPORT=infiniband; make -j; sudo ctest`.
   Here, `infiniband` should be replaced with `raw` for Mellanox Ethernet NICs,
   or `dpdk` for Intel Ethernet NICs.
 * Generate the documentation: `doxygen`
 * Run the `hello_world` application:
   * `cd hello_world`
   * Edit the server and client hostnames in `common.h` 
   * `make`
   * Run `./server` at the server, and `./client` at the client

## Supported NICs:
 * UDP over Ethernet mode (`DTRANSPORT=raw`):
   * ConnectX-4 or newer Mellanox Ethernet NICs
   * ConnectX-3 and older Mellanox NICs are supported in eRPC's RoCE mode
   * Support for other NICs via DPDK is under development
 * InfiniBand mode (`DTRANSPORT=infiniband`): Any InfiniBand-compliant NICs
 * RoCE mode: Any RoCE-compilant NICs
 * Mellanox drivers specially optimized for eRPC are available in the `drivers`
   directory.

## Configuring and running the provided applications
 * The `apps` directory contains a suite of benchmarks and examples. The
   instructions below are for this suite of applications. eRPC can also be
   simply linked as a library instead (see `hello_world/` for an example).
 * To build an application, change the contents of `scripts/autorun_app_file`
   to one of the available directory names in `apps/`. Then generate a Makefile
   using `cmake . -DPERF=ON/OFF -DTRANSPORT=raw/infiniband/dpdk`. 
 * Each application directory in `apps/` contains a config file
   that must specify all flags defined in `apps/apps_common.h`. For example,
   `num_processes` specifies the total number of eRPC processes in the cluster.
 * The URIs of eRPC processes in the cluster are specified in
   `scripts/autorun_process_file`. Each line in this file must be
   `<hostname> <management udp port> <numa_node>`. One eRPC process is allowed
   per NUMA node. See `scripts/gen_autorun_process_file.sh` for how to generate
   this file.
 * Run `scripts/do.sh` for each process:
   * With single-CPU machines: `num_processes` machines are needed.
     Run `scripts/do.sh <i> 0` on machine `i` in `{0, ..., num_processes - 1}`.
   * With dual-CPU machines: `num_machines = ceil(num_processes / 2)` machines
     are needed. Run `scripts/do.sh <i> <i % 2>` on machine i in
     `{0, ..., num_machines - 1}`.
 * To automatically run an application at all processes in
   `scripts/autorun_process_file`, run `scripts/run-all.sh`. Application
   statistics generated in a run can be collected using `scripts/proc-out.sh`.

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

