#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_comps_st() {
  assert(in_dispatch());
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) return;

  // Measure RX burst size
  dpath_stat_inc(dpath_stats.rx_burst_calls, 1);
  dpath_stat_inc(dpath_stats.pkts_rx, num_pkts);

  size_t batch_rx_tsc = 0;
  if (kCcOptBatchTsc) batch_rx_tsc = dpath_rdtsc();

  for (size_t i = 0; i < num_pkts; i++) {
    auto *pkthdr = reinterpret_cast<pkthdr_t *>(rx_ring[rx_ring_head]);
    rx_ring_head = (rx_ring_head + 1) % Transport::kNumRxRingEntries;

    assert(pkthdr->check_magic());
    assert(pkthdr->msg_size <= kMaxMsgSize);  // msg_size can be 0 here

    uint16_t session_num = pkthdr->dest_session_num;  // The local session
    assert(session_num < session_vec.size());

    Session *session = session_vec[session_num];
    if (unlikely(session == nullptr)) {
      LOG_WARN(
          "eRPC Rpc %u: Warning: Received packet %s for buried session. "
          "Dropping packet.\n",
          rpc_id, pkthdr->to_string().c_str());
      continue;
    }

    if (unlikely(!session->is_connected())) {
      LOG_WARN(
          "eRPC Rpc %u: Warning: Received packet %s for unconnected "
          "session (state is %s). Dropping packet.\n",
          rpc_id, pkthdr->to_string().c_str(),
          session_state_str(session->state).c_str());
      continue;
    }

    // If we are here, we have a valid packet for a connected session
    LOG_TRACE("eRPC Rpc %u: Received packet %s.\n", rpc_id,
              pkthdr->to_string().c_str());

    size_t sslot_i = pkthdr->req_num % kSessionReqWindow;  // Bit shift
    SSlot *sslot = &session->sslot_arr[sslot_i];

    // Process control packets
    if (pkthdr->msg_size == 0) {
      assert(pkthdr->is_req_for_resp() || pkthdr->is_expl_cr());
      pkthdr->is_req_for_resp()
          ? process_req_for_resp_st(sslot, pkthdr)
          : process_expl_cr_st(sslot, pkthdr,
                               kCcOptBatchTsc ? batch_rx_tsc : dpath_rdtsc());
      continue;
    }

    // If we're here, this is a data packet
    assert(pkthdr->is_req() || pkthdr->is_resp());

    if (pkthdr->msg_size <= TTr::kMaxDataPerPkt) {
      assert(pkthdr->pkt_num == 0);
      pkthdr->is_req()
          ? process_small_req_st(sslot, pkthdr)
          : process_small_resp_st(
                sslot, pkthdr, kCcOptBatchTsc ? batch_rx_tsc : dpath_rdtsc());
    } else {
      pkthdr->is_req()
          ? process_large_req_one_st(sslot, pkthdr)
          : process_large_resp_one_st(
                sslot, pkthdr, kCcOptBatchTsc ? batch_rx_tsc : dpath_rdtsc());
    }
  }

  // Technically, these RECVs can be posted immediately after rx_burst(), or
  // even in the rx_burst() code.
  transport->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::submit_background_st(SSlot *sslot, Nexus::BgWorkItemType wi_type,
                                    size_t bg_etid) {
  assert(in_dispatch());
  assert(bg_etid < nexus->num_bg_threads || bg_etid == kInvalidBgETid);
  assert(nexus->num_bg_threads > 0);

  if (bg_etid == kInvalidBgETid) {
    // Background thread was not specified, so choose one at random
    bg_etid = fast_rand.next_u32() % nexus->num_bg_threads;
  }

  auto *req_queue = nexus_hook.bg_req_queue_arr[bg_etid];
  req_queue->unlocked_push(Nexus::BgWorkItem(wi_type, rpc_id, context, sslot));
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc
