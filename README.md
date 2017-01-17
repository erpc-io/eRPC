## API notes
 * Disconnecting sessions
   * Only a connected session can be disconnected. So the requester must
     receive the connect response before sending the disconnect.

## TODOs
 * Use kInvalidSessionNum instead of std::numeric_limits<>. Use this in
   is_session_ptr_client and is_session_ptr_server.
