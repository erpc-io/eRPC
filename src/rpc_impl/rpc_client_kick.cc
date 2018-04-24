#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::client_kick_st(SSlot *sslot) {
  assert(in_dispatch());
  Session *session = sslot->session;
  auto &credits = session->client_info.credits;
  assert(credits > 0);  // Precondition

  const bool bypass_wheel =
      !kCcPacing || (kCcOptWheelBypass && session->is_uncongested());

  MsgBuffer *req_msgbuf = sslot->tx_msgbuf;
  auto &ci = sslot->client_info;

  if (likely(ci.num_tx < req_msgbuf->num_pkts)) {
    // We still have request packets to send. At least one will be sent.
    size_t sending = std::min(credits, req_msgbuf->num_pkts - ci.num_tx);

    for (size_t _x = 0; _x < sending; _x++) {
      const size_t pkt_idx = ci.num_tx, pkt_num = ci.num_tx;

      if (bypass_wheel) {
        enqueue_pkt_tx_burst_st(sslot, pkt_idx,
                                &ci.tx_ts[pkt_num % kSessionCredits]);
      } else {
        size_t pkt_sz = req_msgbuf->get_pkt_size<TTr::kMaxDataPerPkt>(pkt_idx);
        size_t ref_tsc = dpath_rdtsc();
        size_t abs_tx_tsc = session->cc_getupdate_tx_tsc(ref_tsc, pkt_sz);
        wheel->insert(wheel_ent_t(sslot, pkt_num), ref_tsc, abs_tx_tsc);

        LOG_CC("eRPC Rpc %u: Req num %zu, pkt num %zu, abs TX %.3f us.\n",
               rpc_id, sslot->cur_req_num, pkt_num,
               to_usec(abs_tx_tsc - creation_tsc, freq_ghz));
      }

      credits--;
      ci.num_tx++;
    }
  } else {
    // We've sent all request packets and now we must send more. This means that
    // we have recieved the first response packet, but not the entire response.
    // The latter means that a background contn. cannot invalidate resp_msgbuf.
    assert(ci.num_rx >= req_msgbuf->num_pkts);
    MsgBuffer *resp_msgbuf = ci.resp_msgbuf;
    assert(resp_msgbuf->is_dynamic_and_matches(sslot->tx_msgbuf));
    assert(ci.num_rx < wire_pkts(req_msgbuf, resp_msgbuf));

    // TODO: Pace RFRs
    size_t rfr_pending = wire_pkts(req_msgbuf, resp_msgbuf) - ci.num_tx;
    size_t sending = std::min(credits, rfr_pending);  // > 0
    for (size_t i = 0; i < sending; i++) {
      enqueue_rfr_st(sslot, resp_msgbuf->get_pkthdr_0());
    }
  }
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc
