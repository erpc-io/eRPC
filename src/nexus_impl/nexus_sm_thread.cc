#include "nexus.h"

namespace ERpc {

/// The number of ENet channels per ENet peer. We don't use multiple channels.
static constexpr size_t kENetChannels = 1;

/// Amount of time to block for ENet events
static constexpr size_t kSmThreadEventLoopMs = 1;

// Implementation notes:
//
// A Nexus can have two ENet peers established to a host: a client-mode peer
// used for sending session management requests, and a server-mode peer used for
// responding to these requests.
//
// A client-mode peer is created to each host that we create a client-mode
// session to. Client-mode peers have non-null peer->data, and are recorded in
// the SM thread context maps.
//
// A server mode peer is created when we get a ENet connect event from a
// client-mode peers. Server-mode peers have null peer-data, and are not
// recorded in the SM threa context maps.

template <class TTr>
void Nexus<TTr>::sm_thread_handle_connect(SmThreadCtx *, ENetEvent *event) {
  assert(event != nullptr);

  ENetPeer *epeer = event->peer;
  if (is_peer_mode_server(epeer)) {
    return;
  }

  // If we're here, this is a client-mode ENet peer
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  assert(!epeer_data->connected);
  assert(epeer_data->work_item_vec.size() > 0);

  epeer_data->connected = true;
  erpc_dprintf(
      "eRPC Nexus: ENet socket connected to %s. Transmitting "
      "%zu queued SM requests.\n",
      epeer_data->rem_hostname.c_str(), epeer_data->work_item_vec.size());

  // Transmit work items queued while waiting for connection
  for (auto wi : epeer_data->work_item_vec) {
    assert(wi.sm_pkt->is_req());
    sm_thread_tx_and_free(wi);
  }

  epeer_data->work_item_vec.clear();
}

template <class TTr>
void Nexus<TTr>::sm_thread_handle_disconnect(SmThreadCtx *ctx,
                                             ENetEvent *event) {
  assert(ctx != nullptr);
  assert(event != nullptr);

  ENetPeer *epeer = event->peer;  // Freed by ENet
  uint32_t epeer_ip = epeer->address.host;
  if (is_peer_mode_server(epeer)) {
    return;
  }

  // If we're here, this is a client mode peer, so we have mappings
  assert(ctx->ip_map.count(epeer_ip) > 0);
  std::string hostname = ctx->ip_map[epeer_ip];
  assert(ctx->name_map[hostname] == epeer);

  SmENetPeerData *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  if (!epeer_data->connected) {
    // This peer didn't connect successfully, so try again. Carry over the
    // ENet peer data from the previous peer to the new peer.
    erpc_dprintf(
        "eRPC Nexus: ENet socket disconnected from %s. Reconnecting.\n",
        hostname.c_str());

    ENetAddress rem_address;
    rem_address.port = ctx->mgmt_udp_port;
    if (enet_address_set_host(&rem_address, epeer_data->rem_hostname.c_str()) !=
        0) {
      throw std::runtime_error("eRPC Nexus: ENet Failed to resolve address " +
                               epeer_data->rem_hostname);
    }

    ENetPeer *new_epeer =
        enet_host_connect(ctx->enet_host, &rem_address, kENetChannels, 0);
    for (SmWorkItem wi : epeer_data->work_item_vec) {
      assert(wi.epeer == epeer);
      wi.epeer = new_epeer;
    }

    new_epeer->data = epeer->data;
  } else {
    erpc_dprintf(
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
  assert(event != nullptr);
  assert(event->packet->dataLength == sizeof(SmPkt));

  ENetPeer *epeer = event->peer;

  if (!is_peer_mode_server(epeer)) {
    // This is an event for a client-mode peer, so we have mappings
    assert(ctx->ip_map.count(epeer->address.host) > 0);
    assert(ctx->name_map[ctx->ip_map[epeer->address.host]] == epeer);
  }

  // Copy out the ENet packet - this gets freed by the Rpc thread
  SmPkt *sm_pkt = new SmPkt();
  memcpy(static_cast<void *>(sm_pkt), event->packet->data, sizeof(SmPkt));
  enet_packet_destroy(event->packet);
  assert(sm_pkt_type_is_valid(sm_pkt->pkt_type));

  // Handle reset peer request here, since it's not passed to any Rpc
  if (sm_pkt->pkt_type == SmPktType::kFaultResetPeerReq) {
    erpc_dprintf(
        "eRPC Nexus: Received reset-remote-peer fault from Rpc [%s, %u]. "
        "Forcefully resetting ENet peer.\n",
        sm_pkt->client.hostname, sm_pkt->client.rpc_id);
    enet_peer_reset(epeer);
    return;
  }

  bool is_sm_req = sm_pkt->is_req();
  uint8_t target_rpc_id =
      is_sm_req ? sm_pkt->server.rpc_id : sm_pkt->client.rpc_id;

  // Lock the Nexus to prevent Rpc registration while we lookup the hook
  ctx->nexus_lock->lock();
  Hook *target_hook = const_cast<Hook *>(ctx->reg_hooks_arr[target_rpc_id]);

  if (target_hook == nullptr) {
    // We don't have an Rpc object for the target Rpc. If sm_pkt is a request,
    // send a response if possible. Ignore if sm_pkt is a response.
    if (is_sm_req) {
      // We don't handle this error for fault-injection session management reqs
      assert(sm_pkt_type_req_has_resp(sm_pkt->pkt_type));

      erpc_dprintf(
          "eRPC Nexus: Received session management request for invalid "
          "Rpc %u from Rpc [%s, %u]. Sending response.\n",
          target_rpc_id, sm_pkt->client.hostname, sm_pkt->client.rpc_id);

      sm_pkt->pkt_type = sm_pkt_type_req_to_resp(sm_pkt->pkt_type);
      sm_pkt->err_type = SmErrType::kInvalidRemoteRpcId;

      // Create a fake (invalid) work item for sm_thread_tx_and_free
      SmWorkItem temp_wi(kInvalidRpcId, sm_pkt, epeer);
      sm_thread_tx_and_free(temp_wi);  // This frees sm_pkt
    } else {
      erpc_dprintf(
          "eRPC Nexus: Received session management response for invalid "
          "Rpc %u from Rpc [%s, %u]. Ignoring.\n",
          target_rpc_id, sm_pkt->client.hostname, sm_pkt->client.rpc_id);
      delete sm_pkt;
    }

    ctx->nexus_lock->unlock();
    return;
  }

  // Submit a work item to the target Rpc. The Rpc thread frees sm_pkt.
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
void Nexus<TTr>::sm_thread_tx_and_free(SmWorkItem &wi) {
  assert(wi.epeer != nullptr);

  SmPkt *sm_pkt = wi.sm_pkt;
  assert(sm_pkt != nullptr);

  // If the work item uses a client-mode peer, the peer must be connected
  if (!is_peer_mode_server(wi.epeer)) {
    assert(static_cast<SmENetPeerData *>(wi.epeer->data)->connected);
  }

  // Create the packet to send
  ENetPacket *enet_pkt =
      enet_packet_create(sm_pkt, sizeof(SmPkt), ENET_PACKET_FLAG_RELIABLE);
  if (enet_pkt == nullptr) {
    throw std::runtime_error("eRPC Nexus: Failed to create ENet packet.");
  }
  delete sm_pkt;

  if (enet_peer_send(wi.epeer, 0, enet_pkt) != 0) {
    throw std::runtime_error("eRPC Nexus: Failed to send ENet packet.");
  }
}

template <class TTr>
void Nexus<TTr>::sm_thread_tx(SmThreadCtx *ctx) {
  assert(ctx != nullptr);

  if (ctx->sm_tx_list.size == 0) {
    return;
  }

  ctx->sm_tx_list.lock();

  for (SmWorkItem &wi : ctx->sm_tx_list.list) {
    assert(wi.sm_pkt != nullptr);
    assert(sm_pkt_type_is_valid(wi.sm_pkt->pkt_type));

    if (wi.sm_pkt->is_req()) {
      // Transmit a session management request. wi.epeer must be filled here
      // because Rpc threads don't have ENet peer information.
      assert(wi.epeer == nullptr);
      std::string rem_hostname = std::string(wi.sm_pkt->server.hostname);

      if (ctx->name_map.count(rem_hostname) > 0) {
        // We already have a client-mode ENet peer to this host
        wi.epeer = ctx->name_map[rem_hostname];

        // Wait if the peer is not yet connected
        auto *epeer_data = static_cast<SmENetPeerData *>(wi.epeer->data);
        if (!epeer_data->connected) {
          epeer_data->work_item_vec.push_back(wi);
        } else {
          sm_thread_tx_and_free(wi);
        }
      } else {
        // We don't have a client-mode ENet peer to this host, so create one
        ENetAddress rem_address;
        rem_address.port = ctx->mgmt_udp_port;
        if (enet_address_set_host(&rem_address, rem_hostname.c_str()) != 0) {
          throw std::runtime_error(
              "eRPC Nexus: ENet Failed to resolve address " + rem_hostname);
        }

        wi.epeer =
            enet_host_connect(ctx->enet_host, &rem_address, kENetChannels, 0);
        if (wi.epeer == nullptr) {
          throw std::runtime_error("eRPC Nexus: Failed to connect ENet to " +
                                   rem_hostname);
        }

        // Reduce ENet peer timeout. This needs more work (e.g., what values
        // can we safely use without false positives?)
        enet_peer_timeout(wi.epeer, 1, 1, 100);

        // Add the peer to mappings to avoid creating a duplicate peer
        ctx->name_map[rem_hostname] = wi.epeer;
        ctx->ip_map[rem_address.host] = rem_hostname;

        // Save the work item - we'll transmit it when we get connected
        wi.epeer->data = new SmENetPeerData();
        auto *epeer_data = static_cast<SmENetPeerData *>(wi.epeer->data);
        epeer_data->rem_hostname = rem_hostname;
        epeer_data->connected = false;

        epeer_data->work_item_vec.push_back(wi);
      }
    } else {
      // Transmit a session management response
      assert(wi.epeer != nullptr);
      sm_thread_tx_and_free(wi);
    }
  }

  ctx->sm_tx_list.locked_clear();
  ctx->sm_tx_list.unlock();
}

template <class TTr>
void Nexus<TTr>::sm_thread_func(SmThreadCtx *ctx) {
  assert(ctx != nullptr);

  // Create an ENet socket that remote nodes can connect to
  if (enet_initialize() != 0) {
    throw std::runtime_error("eRPC Nexus: Failed to initialize ENet.");
  }

  ENetAddress address;
  enet_address_set_host(&address, "localhost");
  address.host = ENET_HOST_ANY;
  address.port = ctx->mgmt_udp_port;

  ctx->enet_host =
      enet_host_create(&address, kMaxNumMachines, kENetChannels, 0, 0);
  if (ctx->enet_host == nullptr) {
    throw std::runtime_error("eRPC Nexus: Failed to create ENet host.");
  }

  // This is not a busy loop, since sm_thread_rx() blocks for several ms
  while (*ctx->kill_switch == false) {
    sm_thread_tx(ctx);
    sm_thread_rx(ctx);
  }

  erpc_dprintf_noargs("eRPC Nexus: Session management thread exiting.\n");

  // ENet cleanup
  enet_host_destroy(ctx->enet_host);
  enet_deinitialize();

  return;
}

}  // End ERpc
