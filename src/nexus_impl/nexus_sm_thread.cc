#include "nexus.h"

namespace ERpc {

/// Amount of time to block for ENet events
static constexpr size_t kSmThreadEventLoopMs = 20;

template <class TTr>
void Nexus<TTr>::sm_thread_handle_connect(SmThreadCtx *, ENetEvent *event) {
  assert(event != nullptr);

  ENetPeer *epeer = event->peer;
  if (epeer->data == nullptr) {
    // This is a connect event for a server mode peer
    epeer->data = new SmENetPeerData(SmENetPeerMode::kServer);
    static_cast<SmENetPeerData *>(epeer->data)->connected = true;
    return;
  }

  // If we're here, this is a client-mode ENet peer
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  assert(!epeer_data->connected);
  assert(epeer_data->client.tx_queue.size() > 0);

  epeer_data->connected = true;
  LOG_INFO(
      "eRPC Nexus: ENet socket connected to %s. Transmitting "
      "%zu queued SM requests.\n",
      epeer_data->rem_hostname.c_str(), epeer_data->client.tx_queue.size());

  // Transmit work items queued while waiting for connection
  for (SmWorkItem &wi : epeer_data->client.tx_queue) {
    assert(wi.sm_pkt.is_req());
    sm_thread_tx_one(wi);
  }

  epeer_data->client.tx_queue.clear();
}

template <class TTr>
void Nexus<TTr>::sm_thread_handle_disconnect(SmThreadCtx *ctx,
                                             ENetEvent *event) {
  assert(ctx != nullptr);
  assert(event != nullptr);

  ENetPeer *epeer = event->peer;  // Freed by ENet
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  if (epeer_data->is_server()) return;

  // If we're here, this is a client mode peer, so we have mappings
  uint32_t epeer_ip = epeer->address.host;
  assert(ctx->ip_map.count(epeer_ip) > 0);
  std::string hostname = ctx->ip_map[epeer_ip];
  assert(ctx->name_map[hostname] == epeer);

  if (!epeer_data->connected) {
    // This peer didn't ever connect successfully, so try again. Carry over the
    // ENet peer data from the previous peer to the new peer.
    LOG_INFO("eRPC Nexus: ENet failed to connect peer to %s. Reconnecting.\n",
             hostname.c_str());

    ENetAddress rem_address;
    rem_address.port = ctx->mgmt_udp_port;
    int ret =
        enet_address_set_host(&rem_address, epeer_data->rem_hostname.c_str());
    rt_assert(ret == 0, "ENet failed to resolve " + epeer_data->rem_hostname);

    ENetPeer *new_epeer = enet_host_connect(ctx->enet_host, &rem_address, 0, 0);
    rt_assert(new_epeer != nullptr,
              "Failed to connect ENet to " + epeer_data->rem_hostname);

    for (SmWorkItem &wi : epeer_data->client.tx_queue) {
      assert(wi.epeer == epeer);
      wi.epeer = new_epeer;
    }

    new_epeer->data = epeer->data;
  } else {
    LOG_INFO(
        "eRPC Nexus: ENet socket disconnected from %s. Not reconnecting.\n",
        hostname.c_str());

    // XXX: Do something with outstanding SM requests on this peer (it could
    // be a session connect request). There may be requests from many sessions.

    // Remove from mappings and free memory
    ctx->ip_map.erase(epeer_ip);
    ctx->name_map.erase(hostname);
    delete static_cast<SmENetPeerData *>(epeer->data);
    epeer->data = nullptr;
  }

  return;
}

template <class TTr>
void Nexus<TTr>::sm_thread_handle_receive(SmThreadCtx *ctx, ENetEvent *event) {
  assert(ctx != nullptr);
  assert(event != nullptr && event->peer != nullptr);
  assert(event->peer->data != nullptr);

  // Copy the session management packet
  assert(event->packet->dataLength == sizeof(SmPkt));
  SmPkt sm_pkt;
  memcpy(static_cast<void *>(&sm_pkt), event->packet->data, sizeof(SmPkt));
  enet_packet_destroy(event->packet);

  LOG_INFO("eRPC Nexus: Received SM packet (type %s, sender %s).\n",
           sm_pkt_type_str(sm_pkt.pkt_type).c_str(),
           sm_pkt_type_is_req(sm_pkt.pkt_type) ? sm_pkt.client.hostname
                                               : sm_pkt.server.hostname);

  ENetPeer *epeer = event->peer;
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);

  if (epeer_data->is_client()) {
    // We have mappings for client-mode peers
    assert(ctx->ip_map.count(epeer->address.host) > 0);
    assert(ctx->name_map[ctx->ip_map[epeer->address.host]] == epeer);
  }

  // Handle reset peer request here, since it's not passed to any Rpc
  if (sm_pkt.pkt_type == SmPktType::kFaultResetPeerReq) {
    LOG_WARN(
        "eRPC Nexus: Received reset-remote-peer fault from Rpc [%s, %u]. "
        "Forcefully resetting ENet peer.\n",
        sm_pkt.client.hostname, sm_pkt.client.rpc_id);
    enet_peer_reset(epeer);
    return;
  }

  bool is_sm_req = sm_pkt.is_req();
  uint8_t target_rpc_id =
      is_sm_req ? sm_pkt.server.rpc_id : sm_pkt.client.rpc_id;

  // Lock the Nexus to prevent Rpc registration while we lookup the hook
  ctx->nexus_lock->lock();
  Hook *target_hook = const_cast<Hook *>(ctx->reg_hooks_arr[target_rpc_id]);

  if (target_hook == nullptr) {
    // We don't have an Rpc object for the target Rpc. If sm_pkt is a request,
    // send a response if possible. Ignore if sm_pkt is a response.
    if (is_sm_req) {
      // We don't handle this error for fault-injection session management reqs
      assert(sm_pkt_type_req_has_resp(sm_pkt.pkt_type));

      LOG_WARN(
          "eRPC Nexus: Received session management request for invalid "
          "Rpc %u from Rpc [%s, %u]. Sending response.\n",
          target_rpc_id, sm_pkt.client.hostname, sm_pkt.client.rpc_id);

      sm_pkt.pkt_type = sm_pkt_type_req_to_resp(sm_pkt.pkt_type);
      sm_pkt.err_type = SmErrType::kInvalidRemoteRpcId;

      // Create a fake (invalid) work item for sm_thread_tx_one
      SmWorkItem temp_wi(kInvalidRpcId, sm_pkt, epeer);
      sm_thread_tx_one(temp_wi);  // This frees sm_pkt
    } else {
      LOG_WARN(
          "eRPC Nexus: Received session management response for invalid "
          "Rpc %u from Rpc [%s, %u]. Ignoring.\n",
          target_rpc_id, sm_pkt.client.hostname, sm_pkt.client.rpc_id);
    }

    ctx->nexus_lock->unlock();
    return;
  }

  // Submit a work item to the target Rpc
  if (is_sm_req) {
    SmWorkItem wi(target_rpc_id, sm_pkt, event->peer);
    target_hook->sm_rx_list.unlocked_push_back(wi);
  } else {
    SmWorkItem wi(target_rpc_id, sm_pkt, nullptr);
    target_hook->sm_rx_list.unlocked_push_back(wi);
  }

  ctx->nexus_lock->unlock();
  return;
}

template <class TTr>
void Nexus<TTr>::sm_thread_rx(SmThreadCtx *ctx) {
  assert(ctx != nullptr);

  ENetEvent event;
  int ret = enet_host_service(ctx->enet_host, &event, kSmThreadEventLoopMs);
  assert(ret >= 0);

  if (ret > 0) {
    // Process the event
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        sm_thread_handle_connect(ctx, &event);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        sm_thread_handle_disconnect(ctx, &event);
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        sm_thread_handle_receive(ctx, &event);
        break;
      case ENET_EVENT_TYPE_NONE:
        throw std::runtime_error("eRPC Nexus: Unknown ENet event type.\n");
    }
  }
}

template <class TTr>
void Nexus<TTr>::sm_thread_tx_one(SmWorkItem &wi) {
  assert(wi.epeer != nullptr && wi.epeer->data != nullptr);
  assert(static_cast<SmENetPeerData *>(wi.epeer->data)->connected);

  // This copies the packet payload
  ENetPacket *enet_pkt =
      enet_packet_create(&wi.sm_pkt, sizeof(SmPkt), ENET_PACKET_FLAG_RELIABLE);
  rt_assert(enet_pkt != nullptr, "Failed to create ENet packet.");

  rt_assert(enet_peer_send(wi.epeer, 0, enet_pkt) == 0,
            "enet_peer_send() failed");
}

template <class TTr>
void Nexus<TTr>::sm_thread_tx(SmThreadCtx *ctx) {
  assert(ctx != nullptr);
  if (ctx->sm_tx_list->size == 0) return;

  ctx->sm_tx_list->lock();

  for (SmWorkItem &wi : ctx->sm_tx_list->list) {
    assert(sm_pkt_type_is_valid(wi.sm_pkt.pkt_type));

    if (wi.sm_pkt.is_req()) {
      // wi.epeer must be filled here because Rpc threads don't have ENet peer
      // information while issuing requests.
      assert(wi.epeer == nullptr);
      std::string rem_hostname = std::string(wi.sm_pkt.server.hostname);

      if (ctx->name_map.count(rem_hostname) > 0) {
        // We already have a client-mode ENet peer to this host
        wi.epeer = ctx->name_map[rem_hostname];

        // Wait if the peer is not yet connected
        auto *epeer_data = static_cast<SmENetPeerData *>(wi.epeer->data);
        if (!epeer_data->connected) {
          epeer_data->client.tx_queue.push_back(wi);
        } else {
          sm_thread_tx_one(wi);
        }
      } else {
        // We don't have a client-mode ENet peer to this host, so create one
        ENetAddress rem_address;
        rem_address.port = ctx->mgmt_udp_port;
        if (enet_address_set_host(&rem_address, rem_hostname.c_str()) != 0) {
          throw std::runtime_error("ENet failed to resolve " + rem_hostname);
        }

        wi.epeer = enet_host_connect(ctx->enet_host, &rem_address, 0, 0);
        rt_assert(wi.epeer != nullptr,
                  "Failed to connect ENet to " + rem_hostname);

        // XXX: Reduce ENet peer timeout. This needs more work: what values
        // can we safely use without false positives?)
        // enet_peer_timeout(wi.epeer, 1, 1, 500);

        // Add the peer to mappings to avoid creating a duplicate peer
        ctx->name_map[rem_hostname] = wi.epeer;
        ctx->ip_map[rem_address.host] = rem_hostname;

        wi.epeer->data = new SmENetPeerData(SmENetPeerMode::kClient);
        auto *epeer_data = static_cast<SmENetPeerData *>(wi.epeer->data);
        epeer_data->rem_hostname = rem_hostname;

        epeer_data->client.tx_queue.push_back(wi);
      }
    } else {
      // Transmit a session management response
      assert(wi.epeer != nullptr);
      sm_thread_tx_one(wi);
    }
  }

  ctx->sm_tx_list->locked_clear();
  ctx->sm_tx_list->unlock();
}

template <class TTr>
void Nexus<TTr>::sm_thread_func(SmThreadCtx ctx) {
  // Create an ENet socket that remote nodes can connect to
  rt_assert(enet_initialize() == 0, "Failed to initialize ENet");

  ENetAddress address;
  enet_address_set_host(&address, "localhost");
  address.host = ENET_HOST_ANY;
  address.port = ctx.mgmt_udp_port;

  ctx.enet_host = enet_host_create(&address, kMaxNumMachines, 0, 0, 0);
  rt_assert(ctx.enet_host != nullptr, "enet_host_create() failed");

  // This is not a busy loop because sm_thread_rx() blocks for several ms
  while (*ctx.kill_switch == false) {
    sm_thread_tx(&ctx);
    sm_thread_rx(&ctx);
  }

  LOG_INFO("eRPC Nexus: Session management thread exiting.\n");

  // ENet cleanup
  enet_host_destroy(ctx.enet_host);
  enet_deinitialize();

  return;
}

}  // End ERpc
