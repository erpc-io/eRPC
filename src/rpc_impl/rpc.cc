#include "rpc.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, void *context,
                     session_mgmt_handler_t session_mgmt_handler, int app_tid,
                     std::vector<int> fdev_port_vec)
    : nexus(nexus),
      context(context),
      session_mgmt_handler(session_mgmt_handler) {
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
  sm_hook.app_tid = app_tid;
  nexus->register_hook((SessionMgmtHook *)&sm_hook);
}

template <class Transport_>
Rpc<Transport_>::~Rpc() {}

template <class Transport_>
Session *Rpc<Transport_>::create_session(int local_fdev_port_index,
                                         const char *_rem_hostname,
                                         int rem_app_tid,
                                         int rem_fdev_port_index) {
  _unused(local_fdev_port_index);
  _unused(_rem_hostname);
  _unused(rem_app_tid);
  _unused(rem_fdev_port_index);
}

template <class Transport_>
SessionStatus Rpc<Transport_>::connect_session(
    Session *session, session_mgmt_handler_t sm_handler) {
  _unused(session);
  _unused(sm_handler);
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

}  // End ERpc
