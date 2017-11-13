/*
 * @file rpc_sm_helpers.cc
 * @brief Session management helper methods
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::handle_sm_st() {
  assert(in_dispatch());

  // Handle SM work items queued by the session management thread
  MtQueue<SmWorkItem> &queue = nexus_hook.sm_rx_queue;
  const size_t cmds_to_process = queue.size;  // We might re-add to the queue

  for (size_t i = 0; i < cmds_to_process; i++) {
    SmWorkItem wi = queue.unlocked_pop();

    // Reset work items don't have a valid SM packet, so handle them first
    if (wi.is_reset()) {
      assert(wi.reset_rem_hostname.length() > 0);
      bool success = handle_reset_st(wi.reset_rem_hostname);
      if (!success) queue.unlocked_push(wi);
      continue;
    }

    const SmPkt &sm_pkt = wi.sm_pkt;
    assert(sm_pkt_type_is_valid(sm_pkt.pkt_type));

    // The sender of a packet cannot be this Rpc
    if (sm_pkt.is_req()) {
      assert(!(strcmp(sm_pkt.client.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt.client.rpc_id == rpc_id));
    } else {
      assert(!(strcmp(sm_pkt.server.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt.server.rpc_id == rpc_id));
    }

    switch (sm_pkt.pkt_type) {
      case SmPktType::kConnectReq:
        handle_connect_req_st(wi);
        break;
      case SmPktType::kConnectResp:
        handle_connect_resp_st(sm_pkt);
        break;
      case SmPktType::kDisconnectReq:
        handle_disconnect_req_st(wi);
        break;
      case SmPktType::kDisconnectResp:
        handle_disconnect_resp_st(sm_pkt);
        break;
      case SmPktType::kFaultResetPeerReq:
        // This is handled in the Nexus
        throw std::runtime_error("Invalid packet type");
    }
  }
}

template <class TTr>
void Rpc<TTr>::bury_session_st(Session *session) {
  assert(in_dispatch());
  assert(session != nullptr);

  // Free session resources
  for (const SSlot &sslot : session->sslot_arr) {
    free_msg_buffer(sslot.pre_resp_msgbuf);  // Prealloc buf is always valid

    // XXX: Which other MsgBuffers do we need to free? Which MsgBuffers are
    // guaranteed to have been freed at this point?
  }

  session_vec.at(session->local_session_num) = nullptr;
  delete session;  // This does nothing except free the session memory
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_req_st(Session *session, SmPktType pkt_type) {
  assert(in_dispatch());
  assert(session != nullptr && session->is_client());

  SmPkt sm_pkt;
  sm_pkt.pkt_type = pkt_type;
  sm_pkt.client = session->client;
  sm_pkt.server = session->server;

  nexus_hook.sm_tx_queue->unlocked_push(SmWorkItem(rpc_id, sm_pkt));
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_resp_st(const SmWorkItem &req_wi,
                                  SmErrType err_type) {
  assert(in_dispatch());
  assert(req_wi.sm_pkt.is_req());

  SmPkt sm_pkt = req_wi.sm_pkt;
  sm_pkt.pkt_type = sm_pkt_type_req_to_resp(sm_pkt.pkt_type);  // Change to resp
  sm_pkt.err_type = err_type;

  nexus_hook.sm_tx_queue->unlocked_push(SmWorkItem(rpc_id, sm_pkt));
}

}  // End erpc
