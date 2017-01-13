## API notes
 * Disconnecting sessions
   * Only a connected session can be disconnected. So the requester must
     receive the connect response before sending the disconnect.

## TODOs
 * Add retransmissions for session management requests. Keep a vector of
   in-progress sessions in the Rpc and check on entering the event loop.
    * The vector should usually be empty, so common-case perf cost is low.
