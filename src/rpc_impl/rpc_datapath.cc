/**
 * @file rpc_datapath.cc
 * @brief Performance-critical Rpc datapath functions
 */

#include <iostream>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
int Rpc<Transport_>::send_request(Session *session, Buffer pkt_buffer,
                                  size_t req_bytes) {
  assert(session != nullptr && session->role == Session::Role::kClient);
  assert(pkt_buffer.is_valid() && check_pkthdr(pkt_buffer));
  assert(req_bytes > 0);

  return false;
}

}  // End ERpc
