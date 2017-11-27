#include "nexus.h"
#include "util/udp_client.h"
#include "util/udp_server.h"

namespace erpc {

/// Amount of time to block for UDP packets
static constexpr size_t kSmThreadRxBlockMs = 20;
static constexpr size_t kUDPBufferSz = MB(4);

void Nexus::sm_thread_func(SmThreadCtx ctx) {
  UDPServer<SmPkt> udp_server(ctx.mgmt_udp_port, kSmThreadRxBlockMs,
                              kUDPBufferSz);
  UDPClient<SmPkt> udp_client;

  // This is not a busy loop because of recv_blocking()
  while (*ctx.kill_switch == false) {
    SmPkt sm_pkt;
    ssize_t ret = udp_server.recv_blocking(sm_pkt);

    if (ret >= 0) {
      rt_assert(static_cast<size_t>(ret) == sizeof(sm_pkt),
                "eRPC Nexus: Invalid SM packet RX size.");

      LOG_INFO("eRPC Nexus: Received SM packet %s\n",
               sm_pkt.to_string().c_str());

      uint8_t target_rpc_id =
          sm_pkt.is_req() ? sm_pkt.server.rpc_id : sm_pkt.client.rpc_id;

      // Lock the Nexus to prevent Rpc registration while we lookup the hook
      ctx.nexus_lock->lock();
      Hook *target_hook = const_cast<Hook *>(ctx.reg_hooks_arr[target_rpc_id]);

      if (target_hook != nullptr) {
        target_hook->sm_rx_queue.unlocked_push(sm_pkt);
      } else {
        // We don't have an Rpc object for the target Rpc. Send a response iff
        // it's a request packet.
        if (sm_pkt.is_req()) {
          LOG_WARN(
              "eRPC Nexus: Received session management request for invalid "
              "Rpc %u from %s. Sending response.\n",
              target_rpc_id, sm_pkt.client.name().c_str());

          const SmPkt resp_sm_pkt =
              sm_construct_resp(sm_pkt, SmErrType::kInvalidRemoteRpcId);

          udp_client.send(resp_sm_pkt.client.hostname, ctx.mgmt_udp_port,
                          resp_sm_pkt);
        } else {
          LOG_WARN(
              "eRPC Nexus: Received session management response for invalid "
              "Rpc %u from %s. Dropping.\n",
              target_rpc_id, sm_pkt.client.name().c_str());
        }
      }

      ctx.nexus_lock->unlock();
    }
  }

  LOG_INFO("eRPC Nexus: Session management thread exiting.\n");
  return;
}

}  // End erpc
