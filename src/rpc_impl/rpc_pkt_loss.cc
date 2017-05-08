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
    // Process only connected client sessions
    if (session == nullptr || session->is_server() ||
        !session->is_connected()) {
      continue;
    }

    for (SSlot &sslot : session->sslot_arr) {
      // Ignore sslots that don't have a request with an incomplete response
      if (sslot.tx_msgbuf == nullptr) continue;

      // If we're here, sslot has an active request and it isn't recovering
      assert(sslot.tx_msgbuf->get_req_num() == sslot.cur_req_num);

      size_t cycles_since_enqueue = rdtsc() - sslot.client_info.enqueue_req_ts;
      size_t ms_since_enqueue = to_msec(cycles_since_enqueue, nexus->freq_ghz);

      if (ms_since_enqueue >= kPktLossTimeoutMs) {
        char issue_msg[kMaxIssueMsgLen];  // The basic issue message
        sprintf(issue_msg,
                "eRPC Rpc %u: Packet loss suspected for session %u, "
                "req num %zu. Issue",
                rpc_id, session->local_session_num,
                sslot.tx_msgbuf->get_req_num());
      }
    }
  }
}

}  // End ERpc
