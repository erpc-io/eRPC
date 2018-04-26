#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::kick_req_st(SSlot *sslot) {
  assert(in_dispatch());
  auto &credits = sslot->session->client_info.credits;
  assert(credits > 0);  // Precondition

  auto &ci = sslot->client_info;
  size_t sending = std::min(credits, sslot->tx_msgbuf->num_pkts - ci.num_tx);
  bool bypass = can_bypass_wheel(sslot);

  for (size_t _x = 0; _x < sending; _x++) {
    const size_t pkt_idx = ci.num_tx, pkt_num = ci.num_tx;

    if (bypass) {
      enqueue_pkt_tx_burst_st(sslot, pkt_idx,
                              &ci.tx_ts[pkt_num % kSessionCredits]);
      ci.num_tx++;
    } else {
      enqueue_wheel_st(
          sslot, sslot->tx_msgbuf->get_pkt_size<TTr::kMaxDataPerPkt>(pkt_idx));
      // ci.num_tx will be bumped when we reap this sslot from the wheel
    }

    credits--;
  }
}

// We're asked to send RFRs, which means that we have recieved the first
// response packet, but not the entire response. The latter implies that a
// background continuation cannot invalidate resp_msgbuf.
template <class TTr>
void Rpc<TTr>::kick_rfr_st(SSlot *sslot) {
  assert(in_dispatch());
  auto &credits = sslot->session->client_info.credits;
  auto &ci = sslot->client_info;

  assert(credits > 0);  // Precondition
  assert(ci.num_rx >= sslot->tx_msgbuf->num_pkts);
  assert(ci.num_rx < wire_pkts(sslot->tx_msgbuf, ci.resp_msgbuf));

  // TODO: Pace RFRs
  size_t rfr_pndng = wire_pkts(sslot->tx_msgbuf, ci.resp_msgbuf) - ci.num_tx;
  size_t sending = std::min(credits, rfr_pndng);  // > 0
  for (size_t _x = 0; _x < sending; _x++) {
    enqueue_rfr_st(sslot, ci.resp_msgbuf->get_pkthdr_0());
    credits--;
    ci.num_tx++;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc
