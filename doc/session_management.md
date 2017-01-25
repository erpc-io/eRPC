Session management
==================

## Generating session management requests
 * Applications generate session management packets using eRPC API calls
   `create_session()` and `destroy_session()`. `create_session()` sends a
   connect request, and `destroy_session()` sends a disconnect request.

## Listening for session management packets
 * Each Nexus installs a `SIGIO` handler and listens for UDP packets on
   port `global_udp_port`.
 * When the handler is invoked by the OS, the Nexus enqueues the packet in the
   session management queue of the Rpc specified by the packet's destination
   TID.

## Handling session management packets
 * To handle session management requests, an Rpc must enter its event loop.
   Although session management packets can be generated and received outside the
   event loop, they can only be handled inside an event loop.
 * On entering the event loop, the Rpc checks its Nexus hook for new session
   management packets. If there are new packets, it invokes the appropriate
   handler, and frees the packet.

## Connect requests and responses
 * A connect request may be generated when the user calls `create_session`.

## Disconnect requests and responses
 * A disconnect request may be generated when the user calls `destroy_session`.
