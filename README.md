## Requirements
 * A C++11 compiler. clang and gcc have been tested.
 * See `scripts/packages.sh` for a list of required software packages.
 * A machine with any InfiniBand NIC, or Mellanox Ethernet NICs (ConnectX-4 or
   newer). Non-Mellanox Ethernet NICs, and Mellanox NICs older than ConnectX-4
   are not supported currently. PRs for these NICs are welcome.
   * It's best to use drivers from Mellanox OFED. Optimized Mellanox drivers for
     eRPC are available in the `drivers` directory, but they are for expert use.
   * Upstream drivers work as well. This requires installing the `ibverbs` and
     `mlx4` userspace packages, and enabling the `mlx4_ib` and `ib_uverbs`
     kernel drivers. On Ubuntu, the incantation is:
      * `apt install libmlx4-dev libibverbs-dev`
      * `modprobe mlx4_ib ib_uverbs`
   * For Connect-IB and newer NICs, use `mlx4` by `mlx5`
 * Unlimited SHM limits, and at least 2048 huge pages on every NUMA node.

## eRPC configuration
 * `src/tweakme.h` defines parameters that govern eRPC's behavior.
   * `CTransport` defines which fabric transport eRPC is compiled for. It can
      be set to `IBTransport` for InfiniBand, or `RawTransport` for Mellanox's
      "Raw" Ethernet transport. `kHeadroom` must be set correspondingly.
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
 * Running the hello world application in `apps/hello`:
   * First compile the eRPC library using CMake.
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


## Code notes
 * The packet number carried in packet headers may not match the packet's index
   in MsgBuffers.
 * If a function throws an exception, its documentation should say so.
 * The Rpc object cannot be destroyed by a background thread or while the
   foreground thread is in the event loop (i.e., by the request handler or
   continuation user code). This means that an initially valid Rpc object will
   stay valid for an interation of the event loop.

## MsgBuffer ownership notes
 * The correctness of this reasoning depends on restrictions on the request
   handler and continuation functions. These functions can only enqueue requests
   and responses, but cannot invoke the event loop. The event loop can modify
   RX ring memory, which is unsafe if a fake buffer's ownership has been passed
   to the application.
 * The two interesting cases first:
   * Request MsgBuffer ownership at server: The request handler loses ownership
     of the request MsgBuffer when it returns, or when it calls
     `enqueue_response()`, whichever is first.
      * When `enqueue_response()` is called, we allow the client to send us
        a new request. So we cannot wait until the request handler returns to
        bury the request MsgBuffer. If we do so, we can bury a newer request's
        MsgBuffer when a background request handler returns.
      * The app loses ownership of the request MsgBuffer when the request
        handler returns, even though eRPC may delay burying it until
        `enqueue_response()` is called. This constraint is required because we
        execute foreground handlers for short requests off the RX ring.
      * Request handle ownership is released on `enqueue_response()`, which may
        happen after the request handler returns.
   * Response MsgBuffer ownership at client: The response MsgBuffer is owned by
     the user.
 * The two less interesting cases:
   * Request MsgBuffer ownership at client: Request MsgBuffers are owned and
     allocated by apps and are never freed by eRPC. The client temporarily loses
     ownership of the request MsgBuffer until the continuation for the request
     is invoked, at which point it is no longer needed for retransmission.
   * Response MsgBuffer ownership at server: The request handler loses ownership
     of the response MsgBuffer when it calls `enqueue_response()`. eRPC will
     free it when the response is no longer needed for retransmission.
 * Burying invariants:
   * At clients, the request MsgBuffer is buried (nullified, since the request
     MsgBuffer is app-owned) on receiving the complete response, and before
     invoking the continuation. So, a null value of `sslot->tx_msgbuf` indicates
     that the sslot is not waiting for a response. Similarly, a non-null value
     of `sslot->tx_msgbuf` indicates that the sslot is waiting for a response.
     This is used to invoke failure continuations during session resets.

## RPC failures
 * On session reset, the client may get continuation-with-failure callbacks.
   These are identical to non-failure continuations, but the user-owned response
   MsgBuffer is temporarily resized to 0. 

## General session management notes
 * Applications generate session management (SM) packets using eRPC API calls
   `create_session()` and `destroy_session()`. `create_session()` sends a
   connect request, and `destroy_session()` sends a disconnect request.
 * The Nexus runs an SM thread that listens for SM packets. This thread enqueues
   received packets into the SM queue of the Rpc specified by the packet's
   destination application TID.
 * To handle SM requests, an Rpc must enter its event loop. Although SM packets
   can be generated outside the event loop, they can only be handled inside an
   event loop.
 * On entering the event loop, the Rpc checks its Nexus hook for new SM
   packets. If there are new packets, it invokes SM handlers.

## Session management invariants
 * XXX: The RPC's session connect/disconnect request/response handlers are
   guaranteed to see a non-resetted session.
 * XXX: A server's reset handler always sees a connected session. This is
   because a server session is immediately destroyed on processing an SM
   disconnect request.
 * XXX: A client's reset handler may see a session in the connected or
   disconnect-in-progress state.

## Session management retries
 * A session management operation is retried by the client in these cases:
   * XXX: Add cases here
   * A session connect request fails because the server does not have the
     requested RPC ID running, and `retry_connect_on_invalid_rpc_id` is set.

## Compile-time optimization notes:
 * Optimizations for `small_rpc_tput`:
   * Each of these optimizations can help a fair bit (5-10%) individually, but
     the benefits do not stack.
   * Set optlevel to extreme.
   * Disable datapath checks and datapath stats.
   * Use O2 intead of O3. Try profile-guided optimization.
   * Use power-of-two number of sessions and avoid Lemire's trick in app.
 * Optimizations for `consensus`:
   * Set session request window to 1, or implement Rpc flush.
   * Set transport max inline size to 120 bytes for ConnectX-3.
 * Unsetting `TESTING` disables support to inject eRPC faults at runtime.

## Short-term TODOs
 * Use TX flush on client-side retransmission
 * Destroy session test fails with `kSessionCredits = 1`, `kSessionReqWindow = 1`
 * in `rpc_enqueue_request.cc`, why don't we always have to set `cont_etid`?
 * Make a list of functions allowed in request and continuation handler. For
   example, `run_ev_loop()` and `delete rpc` are not allowed, and eRPC does not
   guard against these errors.
 * Make apps use BasicAppContext and `basic_sm_handler`.
 * Use `rt_assert` in src and apps
 * In IBTransport, check if MLX environment vars are set. Do it in constructor.
 * RFR sending needs to be paced, so we cannot use `send_rfr_now`.
 * Handle `poll_cq` and `post_send` failures in IBTransport. Do it by moving
   RpcDatapathErrCode from rpc.h to common.h, and using it in IBTransport.
 * Do we need separate `rx_burst()` and `post_recvs()` functions in Transport?

## Long-term TODOs
 * Enable marking an Rpc object as server-only. Such an Rpc object can bypass
   packet loss detection code in the event loop.
 * Handle MsgBuffer allocation failures in eRPC.
 * Create session objects from a hugepage-backed pool to reduce TLB misses.
 * The first packet size limit should be much smaller than MTU to improve RTT
   measurement accuracy (e.g., it could be around 256 bytes). This will need
   many changes, mostly to code that uses TTr::kMaxDataPerPkt.
   * We must ensure that a small response message packet or a credit return
     packet is never delayed by TX queueing (even by congestion control--related
     TX queueing) or we'll mess up RTT measurement.
   * Credit return packets will need the packet number field so that the
     receiver can match RTT.

## Longer-term TODOs
 * Replace exit(-1) in non-destructor code with `rt_assert` IF it doesn't
   reduce perf (`rt_assert` takes a string argument - does that cause overhead?)
 * Optimize `pkthdr_0` filling using preconstructed headers.
 * Need to have a test for session management request timeouts.
