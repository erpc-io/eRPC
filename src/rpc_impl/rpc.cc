#include "rpc.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, erpc_tid_t thread_id,
                     std::vector<int> fdev_port_vec)
    : nexus(nexus) {
  Transport_ *transport = new Transport_();
  _unused(transport);

  if (fdev_port_vec.size() == 0) {
    fprintf(stderr, "eRPC Rpc: FATAL. Rpc created with 0 fabric ports.\n");
    exit(-1);
  }

  if (fdev_port_vec.size() > kMaxFabDevPorts) {
    fprintf(stderr, "eRPC Rpc: FATAL. Only %zu local ports supported.\n",
            kMaxFabDevPorts);
    exit(-1);
  }

  /* Record the requested local ports in an array */
  int i = 0;
  for (int fdev_port : fdev_port_vec) {
    fdev_port_arr[i] = fdev_port;
    i++;
  }

  /* Register a hook with the Nexus */
  sm_hook.thread_id = thread_id;
  nexus->register_hook((SessionManagementHook *)&sm_hook);
}

template <class Transport_>
Rpc<Transport_>::~Rpc() {}

template <class Transport_>
void Rpc<Transport_>::resolve_session(Session *session) {
  transport->send_resolve_session_msg(session);
}

template <class Transport_>
void Rpc<Transport_>::send_request(const Session *session,
                                   const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

template <class Transport_>
void Rpc<Transport_>::send_response(const Session *session,
                                    const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
};

template <class Transport_>
void Rpc<Transport_>::run_event_loop(){};

}  // End ERpc
