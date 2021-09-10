#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::kick_req_st(SSlot *sslot) {
  assert(in_dispatch());
  auto &credits = sslot->session_->client_info_.credits_;
  assert(credits > 0);  // Precondition

  auto &ci = sslot->client_info_;
  size_t sending =
      (std::min)(credits, sslot->tx_msgbuf_->num_pkts_ - ci.num_tx_);
  bool bypass = can_bypass_wheel(sslot);

  for (size_t x = 0; x < sending; x++) {
    if (bypass) {
      enqueue_pkt_tx_burst_st(sslot, ci.num_tx_ /* pkt_idx */,
                              &ci.tx_ts_[ci.num_tx_ % kSessionCredits]);
    } else {
      enqueue_wheel_req_st(sslot, ci.num_tx_);
    }

    ci.num_tx_++;
    credits--;
  }
}

// We're asked to send RFRs, which means that we have recieved the first
// response packet, but not the entire response. The latter implies that a
// background continuation cannot invalidate resp_msgbuf.
template <class TTr>
void Rpc<TTr>::kick_rfr_st(SSlot *sslot) {
  assert(in_dispatch());
  auto &credits = sslot->session_->client_info_.credits_;
  auto &ci = sslot->client_info_;

  assert(credits > 0);  // Precondition
  assert(ci.num_rx_ >= sslot->tx_msgbuf_->num_pkts_);
  assert(ci.num_rx_ < wire_pkts(sslot->tx_msgbuf_, ci.resp_msgbuf_));

  // TODO: Pace RFRs
  size_t rfr_pndng = wire_pkts(sslot->tx_msgbuf_, ci.resp_msgbuf_) - ci.num_tx_;
  size_t sending = (std::min)(credits, rfr_pndng);  // > 0
  for (size_t x = 0; x < sending; x++) {
    enqueue_rfr_st(sslot, ci.resp_msgbuf_->get_pkthdr_0());
    ci.num_tx_++;
    credits--;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc
