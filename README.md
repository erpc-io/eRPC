EA Code notes
 * An important simplifying insight is that all functions invoked by the event
   loop run to completion, i.e., we cannot be preempted.
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

## MsgBuffer ownership notes
 * The correctness of this reasoning depends on restrictions on the request
   handler and continuation functions. These functions can only enqueue requests
   and responses, but cannot invoke the event loop. The event loop can modify
   RX ring memory, which is unsafe if the buffer ownership has been passed to
   the application.
 * At client:
   * Request MsgBuffers are owned/allocated by apps and are never freed by eRPC.
     The client temporarily loses ownership of the request MsgBuffer until the
     continuation for the request is invoked. During this time, the client may
     not modify the request MsgBuffer.
   * A continuation must invoke `release_response()` before returning, which
     buries the response MsgBuffer.
 * At server:
   * The request handler permanently loses ownership of the request MsgBuffer
     when it returns, at which point eRPC may free it.
    * For foreground handlers, eRPC frees the request MsgBuffer when the handler
      returns.
    * For background handlers, the buffer is freed when the user calls
      `enqueue_response()`. This is because the background threads don't have
      access to the Rpc's MsgBuffer burying functions. 
   * The request handler permanently loses ownership of the response MsgBuffer
     when it calls `enqueue_response()`. eRPC will free it when the response
     is no longer needed for retransmission.

## Short-term TODOs
 * Rename `sm_hander` to `sm_handler`.
 * Locking for `enqueue_req` and `enqueue_resp`? How much locking can we avoid?
   (e.g., we need lock in `upsert_datapath_tx_work_queue`.)
 * Which fields of SSlot need to be public?
 * Need background flag for continuations.
 * Do we need free_vec of SSlot indices, or can we just have pointers?
 * Should `datapath_tx_work_queue` contain sslots instead of sessions?
 * Do we need `no_free` versions of sslot burying functions?
 * Handle `poll_cq` and `post_send` failures in IBTransport. Do it by moving
   RpcDatapathErrCode from rpc.h to common.h, and using it in IBTransport.
 * Do we need separate `rx_burst()` and `post_recvs()` functions in Transport?

## Long-term TODOs
 * Optimize mem-copies using `rte_memcpy`.
 * Optimize `pkthdr_0` filling using preconstructed headers.
 * The first packet size limit should be much smaller than MTU to improve RTT
   measurement accuracy (e.g., it could be around 256 bytes). This will need
   many changes, mostly to code that uses TTr::kMaxDataPerPkt.
   * We must ensure that a small response message packet or a credit return
     packet is never delayed by TX queueing (even by congestion control--related
     TX queueing) or we'll mess up RTT measurement.
   * Credit return packets will need the packet number field so that the
     receiver can match RTT.

## Longer-term TODOs
 * Optimize Mellanox drivers `post_send` and `poll_cq`, including memcpy,
   function pointers, and unused opcodes/QP types/cases. If inline size is
   fixed at 60 bytes, optimized that. Add fast RECV posting.
 * Are we losing some performance by using `size_t` instead of `uint32_t` in
   in-memory structs like Buffer and MsgBuffer?
 * Need to have a test for session management request timeouts.
 * What happens in the following case:
   1. Requester sends a connect packet (packet 1). Packet 1 gets delayed.
   2. Requester re-sends the connect packet (packet 2).
   3. Responder receives packet 2 and sends a connect response.
   4. Requester receives connect response and transitions the session to
      kConnected.
   5. Requester successfully disconnects the session.
   6. Packet 1 arrives at the responder. The responder has no way of recalling
      that it already replied to this connect request, since the connect request
      does not have the server's sessio number. (One solution is to let the
      server create the server-side end point and reply with a successful connect
      response - the client can just ignore the response because it will see
      that session_vec[sm_pkt->client.session_num] == nullptr. This causes a
      memory leak at the server since the allocated server-side endpoint will
      never get destroyed.)
 * Use pool for session object allocation?

## Perf notes
 * Flags that control performance:
   * kDataPathChecks
   * `small_msg_likely`
