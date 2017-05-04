/**
 * @file rpc_sm_flush_handlers.cc
 * @brief Handlers for session management flush requests and responses
 */
#include "rpc.h"

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_flush_mgmt_req_st(typename Nexus<TTr>::SmWorkItem *wi) {
  assert(in_creator());
  assert(wi != nullptr && wi->epeer != nullptr);

  const SmPkt *sm_pkt = wi->sm_pkt;
  assert(sm_pkt != nullptr && sm_pkt->pkt_type == SmPktType::kFlushMgmtReq);

  auto fm_req_type = static_cast<FlushMgmtReqType>(sm_pkt->gen_data);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received flush management %s request from %s. Issue",
          rpc_id,
          fm_req_type == FlushMgmtReqType::kAcquire ? "acquire" : "release",
          sm_pkt->client.name().c_str());

  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  // Check if the session is still connected
  Session *session = session_vec.at(session_num);
  if (session == nullptr) {
    erpc_dprintf("%s: Session is disconnected. Sending response.\n", issue_msg);
    enqueue_sm_resp_st(wi, SmErrType::kSrvDisconnected);
    return;
  }

  assert(session->is_server() && session->is_connected());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  switch (fm_req_type) {
    case FlushMgmtReqType::kAcquire:
      if (flush_credits_available == 0) {
        erpc_dprintf("%s: Flush credits exhausted. Sending response.\n",
                     issue_msg);
        enqueue_sm_resp_st(wi, SmErrType::kFlushCreditsExhausted);
      } else {
        erpc_dprintf("%s: None. Sending response.\n", issue_msg);
        flush_credits_available--;
        enqueue_sm_resp_st(wi, SmErrType::kNoError);
      }
      return;
    case FlushMgmtReqType::kRelease:
      assert(flush_credits_available < kFlushCredits);
      erpc_dprintf("%s: None. Not sending response\n", issue_msg);
      flush_credits_available++;
      return;
  }

  throw std::runtime_error("Invalid flush management request type");
}

template <class TTr>
void Rpc<TTr>::handle_flush_mgmt_resp_st(SmPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != nullptr);
  assert(sm_pkt->pkt_type == SmPktType::kFlushMgmtResp);

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received flush management acquire response from %s "
          "for session %u. Issue",
          rpc_id, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  // Check if the session is still connected
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  if (session == nullptr || !session->is_connected()) {
    erpc_dprintf("%s: Session is disconnected. Ignoring.\n", issue_msg);
    return;
  }

  assert(session->is_client());
  assert(session->client == sm_pkt->client);
  assert(session->server == sm_pkt->server);

  // XXX: Do something with the response
  if (sm_pkt->err_type == SmErrType::kFlushCreditsExhausted) {
  } else {
    erpc_dprintf("%s: None. Processing response.\n", issue_msg);
  }
}

}  // End ERpc
