## Code notes
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

## API notes

## Short-term TODOs
 * Refactor adding to datapath_tx_work_queue.
 * Refactor session role checking.
 * Handle `poll_cq` and `post_send` failures in IBTransport. Do it by moving
   RpcDatapathErrCode from rpc.h to common.h, and using it in IBTransport.
 * Do we need separate `rx_burst()` and `post_recvs()` functions in Transport?

## Long-term TODOs
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

## Perf notes
 * Flags that control performance:
   * kDataPathChecks
   * `small_msg_likely`



    /* Avoid division for small packets */
    if (small_msg_likely(data_size <= Transport_::kMaxDataPerPkt)) {
      num_pkts = 1;
    } else {
      num_pkts = (data_size + (Transport_::kMaxDataPerPkt - 1)) /
                 Transport_::kMaxDataPerPkt;
    }

