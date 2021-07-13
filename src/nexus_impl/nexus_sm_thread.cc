#include "nexus.h"
#include "util/udp_client.h"
#include "util/udp_server.h"

namespace erpc {

/// Amount of time to block for UDP packets
static constexpr size_t kSmThreadRxBlockMs = 20;

void Nexus::sm_thread_func(SmThreadCtx ctx) {
  UDPServer<SmPkt> udp_server(ctx.sm_udp_port_, kSmThreadRxBlockMs);
  UDPClient<SmPkt> udp_client;

  // This is not a busy loop because of recv_blocking()
  while (*ctx.kill_switch_ == false) {
    SmPkt sm_pkt;
    const size_t ret = udp_server.recv_blocking(sm_pkt);
    if (ret == 0) continue;

    rt_assert(static_cast<size_t>(ret) == sizeof(sm_pkt),
              "eRPC SM thread: Invalid SM packet RX size.");

    if (sm_pkt.pkt_type_ == SmPktType::kUnblock) {
      ERPC_INFO("eRPC SM thread: Received unblock packet.\n");
      continue;
    }

    ERPC_INFO("eRPC SM thread: Received SM packet %s\n",
              sm_pkt.to_string().c_str());

    const uint8_t target_rpc_id =
        sm_pkt.is_req() ? sm_pkt.server_.rpc_id_ : sm_pkt.client_.rpc_id_;

    // Lock the Nexus to prevent Rpc registration while we lookup the hook
    ctx.reg_hooks_lock_->lock();
    Hook *target_hook = const_cast<Hook *>(ctx.reg_hooks_arr_[target_rpc_id]);

    if (target_hook != nullptr) {
      target_hook->sm_rx_queue_.unlocked_push(
          SmWorkItem(target_rpc_id, sm_pkt));
    } else {
      // We don't have an Rpc object for the target Rpc. Send an error
      // response iff it's a request packet.
      if (sm_pkt.is_req()) {
        ERPC_INFO(
            "eRPC SM thread: Received session management request for invalid "
            "Rpc %u from %s. Sending response.\n",
            target_rpc_id, sm_pkt.client_.name().c_str());

        const SmPkt resp_sm_pkt =
            sm_construct_resp(sm_pkt, SmErrType::kInvalidRemoteRpcId);

        udp_client.send(resp_sm_pkt.client_.hostname_,
                        resp_sm_pkt.client_.sm_udp_port_, resp_sm_pkt);
      } else {
        ERPC_INFO(
            "eRPC SM thread: Received session management response for invalid "
            "Rpc %u from %s. Dropping.\n",
            target_rpc_id, sm_pkt.client_.name().c_str());
      }
    }

    ctx.reg_hooks_lock_->unlock();
  }

  ERPC_INFO("eRPC SM thread: Session management thread exiting.\n");
  return;
}

}  // namespace erpc
