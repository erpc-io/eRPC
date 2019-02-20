## Code notes
 * The packet number carried in packet headers may not match the packet's index
   in MsgBuffers.
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

## DPDK notes
 * Ubuntu's DPDK package is incompatible with Mellanox OFED. The main issue
   is that Mellanox OFED ships with experimental features (`ibv_exp_*`) that
   have different names in the upstream `libibverbs-dev` used by Ubuntu DPDK.
   For example, headers for multi-packet RQs are in `verbs_exp.h` in Mellanox
   OFED, but in `mlx5dv.h` in `ibverbs-providers`.
 * DPDK does not allow loopback. (Strangely, loopback works with Mellanox's
   Raw Ethernet transport, but not with DPDK, which internally uses Raw. This
   could because all QPs in the mlx5 PMD use the same device context, whereas
   eRPC's RawTransport uses separate device contexts.) A machine with multiple
   ports is needed unit-test with DPDK.
 * eRPC does not work in Azure as of August 2018: The DPDK driver for ConnectX-3
   NICs does not support any flow steering filters. It might be possible to use
   ConnectX-3 NICs in Ethernet mode with Mellanox's Raw transport, but the
   current Raw transport implementation assumes `mlx5`.

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
 * Optimizations for `consensus`:
   * Set session request window to 1, or implement Rpc flush.
   * Set transport max inline size to 120 bytes for ConnectX-3.

## Immediate TODOs
 * This list should be empty :)
 * `large_msg_test` fails when there are only 512 huge pages. This is OK, but
    the error occurs during memory registration (on InfiniBand). It should occur
    in `alloc_raw`.
 * On slow machines, tests might take too long, so `kTestMaxEventLoopMs` might
   be too short a duration. Ideally we would like to run the test as long as
   we're steadily getting responses. This can be done cleanly in
   `wait_for_rpc_resps_or_timeout`.

## Short-term TODOs
 * Session reset and machine failure detection broke when I changed session
   management to use UDP instead of ENet.
 * `pkt\_loss\_stats` is more hotly-accessed than its placement in `class Rpc`.
 * In `rpc_enqueue_request.cc`, why don't we always have to set `cont_etid`?
 * Make a list of functions allowed in request and continuation handler. For
   example, re-entering the event loop isn't allowed, and eRPC does not
   guard against these errors.
 * Make apps use BasicAppContext and `basic_sm_handler`.
 * Use `rt_assert` in src and apps.
 * In IBTransport, check if MLX environment vars are set. Do it in constructor.
 * RFR sending needs to be paced, so we cannot use `send_rfr_now`.
 * Do we need separate `rx_burst()` and `post_recvs()` functions in Transport?

## Long-term TODOs
 * Fix compilation of `mica_test`. It gets incomplete type errors for MICA in
   YouCompleteMe, and `pthread_create missing` error with DPERF=ON.
 * Sync modded and original mlx4 drivers, similar to mlx5.
 * Enable marking an Rpc object as server-only. Such an Rpc object can bypass
   packet loss detection code in the event loop.
 * Handle MsgBuffer allocation failures in eRPC.
 * Create session objects from a hugepage-backed pool to reduce TLB misses.
 * The first packet size limit could be smaller than MTU to improve RTT
   measurement accuracy (e.g., it could be around 256 bytes). This will need
   many changes, mostly to code that uses TTr::kMaxDataPerPkt.
   * We must ensure that a small response message packet or a credit return
     packet is never delayed by TX queueing (even by congestion control--related
     TX queueing) or we'll mess up RTT measurement.
   * Credit return packets will need the packet number field so that the
     receiver can match RTT.

## Longer-term TODOs
 * Optimize `pkthdr_0` filling using preconstructed headers.
 * Need to have a test for session management request timeouts.
