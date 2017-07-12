## Code notes
 * An important simplifying insight is that all functions invoked by the event
   loop run to completion, i.e., we cannot be preempted, leaving objects in
   an invalid state.
 * Major types
   * Request type: `uint8_t`. 256 request types should be enough.
   * App TIDs: `uint8_t`. We need one per thread, so 256 is enough.
   * Session number: `uint16_t`. We need one per session, and we don't expect
     to support over 65,536 sessions per thread. More sessions can be supported
     with multiple threads.
   * Sequence numbers: `uint64_t` - not `size_t`
   * Numa nodes: `size_t`. These are not transferred over the network, so no
     need to shrink.
 * Use exceptions in constructors. No exceptions in destructors.
 * If a function throws an exception, its documentation should say so.
 * Do not append integers directly to string streams. If the integer is
   `uint8_t`, it will get interpreted as a character.
 * Do not initialize struct/class members in the struct/class definition
   (except maybe in major classes like Rpc/Nexus/Session). This only causes
   confusion.

## Object lifetime notes
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
     of the request MsgBuffer when it returns, at which point eRPC frees it.
     This is true for both foreground and background request handlers.
   * Response MsgBuffer ownership at client: The response MsgBuffer is buried
     when the continuation invokes `release_response()`. A continuation must
     invoke `release_response()` before returning. This is required required so
     that the continuation can enqueue more requests before returning.
 * The two less interesting cases:
   * Request MsgBuffer ownership at client: Request MsgBuffers are owned and
     allocated by apps and are never freed by eRPC. The client temporarily loses
     ownership of the request MsgBuffer until the continuation for the request
     is invoked, at which point it is no longer needed for retransmission.
   * Response MsgBuffer ownership at server: The request handler loses ownership
     of the response MsgBuffer when it calls `enqueue_response()`. eRPC will
     free it when the response is no longer needed for retransmission.
 * Burying invariants:
   * At clients, the request MsgBuffer is buried on receiving the complete
     response, and before invoking the continuation. So, a null value of
     `sslot->tx_msgbuf` indicates that the sslot is not waiting for a response.
     Similarly, a non-null value of `sslot->tx_msgbuf` indicates that the sslot
     is waiting for a response.

## General session management notes
 * Applications generate session management packets using eRPC API calls
   `create_session()` and `destroy_session()`. `create_session()` sends a
   connect request, and `destroy_session()` sends a disconnect request.
 * The Nexus runs a session management thread that listens for session
   management packets. This thread enqueues received packets into the session
   management queue of the Rpc specified by the packet's destination application
   TID.
 * To handle session management requests, an Rpc must enter its event loop.
   Although session management packets can be generated outside the event loop,
   they can only be handled inside an event loop.
 * On entering the event loop, the Rpc checks its Nexus hook for new session
   management packets. If there are new packets, it invokes the appropriate
   session management handler, and frees the packet.

## Session management retries
 * A session management operation is retried by the client in these cases:
   * An ENet disconnect event is received before the connect event. This happens
     when the ENet client tries to connect before the server is running. In
     this case, a new ENet peer is created and the connection is retried.
   * A session connect request fails because the server does not have the
     requested RPC ID running, and `retry_connect_on_invalid_rpc_id` is set.

## Session management packet ownership
 * Rpc threads allocate an SM packet before queueing it to the SM thread. This
   packet is freed by the SM thread when it transmits it using
   `sm_tx_work_item_and_free()`.
    * Rpc threads allocate SM request packets in `enqueue_sm_req()`, and
      response packets in `enqueue_sm_resp()`.
 * The SM thread allocates an SM packet when it gets an ENet RECEIVE event.
   This packet is freed by the Rpc thread, even when it is a request SM packet
   (for which a response SM packet will be "looped back" to the SM thread).
     * The Rpc thread frees SM packets in `handle_session_management_st()`.

## Compile-time optimization notes:
 * Optimizations for `small_rpc_tput`:
   * Each of these optimizations can help a fair bit (5-10%) individually, but
     the benefits do not stack.
   * Set optlevel to extreme.
   * Disable datapath checks and datapath stats.
   * Use O2 intead of O3. Try profile-guided optimization.
   * Use power-of-two number of sessions and avoid Lemire's trick in app.
   * In the continuation function, reduce frequency of scanning for stagnated
     batches.
 * Setting `small_rpc_optlevel` to `small_rpc_optlevel_extreme` will disable
   support for large messages and background threads.
 * Setting `FAULT_INJECTION` to off will disable support to inject eRPC faults
   at runtime.

## Short-term TODOs
 * In IBTransport, check if MLX environment vars are set. Do it in constructor.
 * Try using union for `server_info` and `client_info` in sslot. This causes
   C++ issues bc of non-trivial destructor in anonymous union.
 * RFR sending needs to be paced, so we cannot use `send_rfr_now`.
 * Handle `poll_cq` and `post_send` failures in IBTransport. Do it by moving
   RpcDatapathErrCode from rpc.h to common.h, and using it in IBTransport.
 * Do we need separate `rx_burst()` and `post_recvs()` functions in Transport?

## Long-term TODOs
 * Enable marking an Rpc object as server-only. Such an Rpc object can bypass
   packet loss detection code in the event loop.
 * Optimize mem-copies using `rte_memcpy`.
 * Create session objects from a hugepage-backed pool to reduce TLB misses.
 * The first packet size limit should be much smaller than MTU to improve RTT
   measurement accuracy (e.g., it could be around 256 bytes). This will need
   many changes, mostly to code that uses TTr::kMaxDataPerPkt.
   * We must ensure that a small response message packet or a credit return
     packet is never delayed by TX queueing (even by congestion control--related
     TX queueing) or we'll mess up RTT measurement.
   * Credit return packets will need the packet number field so that the
     receiver can match RTT.
 * Handle MsgBuffer allocation failures in eRPC.

## Longer-term TODOs
 * Optimize Mellanox drivers `post_send` and `poll_cq`, including memcpy,
   function pointers, and unused opcodes/QP types/cases. If inline size is
   fixed at 60 bytes, optimized that. Add fast RECV posting.
 * Less frequent use of `rdtsc()`
 * Optimize `pkthdr_0` filling using preconstructed headers.
 * Are we losing some performance by using `size_t` instead of `uint32_t` in
   in-memory structs like Buffer and MsgBuffer?
 * Need to have a test for session management request timeouts.
 * Use pool for session object allocation?

## Perf notes
 * Flags that control performance:
   * kDataPathChecks
   * `small_rpc_likely`
