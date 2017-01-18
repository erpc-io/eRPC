## Code notes
  * Major types
    * App TIDs: `uint8_t`. We need one per thread, so 256 is enough.
    * Session number: `uint32_t`. We need one per session, and we may need
      more than 65,536.
    * Sequence numbers: `uint64_t` - not `size_t`.

## API notes

## TODOs
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
      that session_vec[sm_pkt->client.session_num] == nullptr.)
