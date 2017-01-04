#include "rpc.h"
#include "nexus.h"

namespace ERpc {

template <class Transport>
Rpc<Transport>::Rpc(Nexus &nexus) : nexus(nexus) {
  nexus.register_hook((SessionManagementHook *)&sm_hook);
}

template <class Transport>
Rpc<Transport>::~Rpc() {}

template <class Transport>
void Rpc<Transport>::send_request(const Session &session,
                                  const Buffer &buffer) {
  _unused(session);
  _unused(buffer);
}

template <class Transport>
void Rpc<Transport>::send_response(const Session &session,
                                   const Buffer &buffer) {
  _unused(session);
  _unused(buffer);
};

template <class Transport>
void Rpc<Transport>::run_event_loop(){};
}
