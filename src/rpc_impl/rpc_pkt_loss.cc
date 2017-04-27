/*
 * @file rpc_pkt_loss.cc
 * @brief Packet loss handling functions
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::pkt_loss_scan_reqs_st() {
  assert(in_creator());

  for (Session *session : session_vec) {
    if (session == nullptr || session->is_server()) {
      continue;
    }
  }
}

}  // End ERpc
