#include "nexus.h"

namespace ERpc {

/// Amount of time to block for ENet evs
static constexpr size_t kSmThreadEventLoopMs = 20;

template <class TTr>
void Nexus<TTr>::sm_thread_on_enet_connect(SmThreadCtx &ctx, ENetEvent *ev) {
  assert(ev != nullptr);

  ENetPeer *epeer = ev->peer;
  if (epeer->data == nullptr) {
    // This is a connect event for a server mode peer
    epeer->data = new SmENetPeerData(SmENetPeerMode::kServer);
    return;
  }

  // If we're here, this is a client-mode ENet peer
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  assert(!epeer_data->client.connected);
  epeer_data->client.connected = true;

  std::string rem_hostname = ctx.ip_map.at(epeer->address.host);
  assert(ctx.name_map.at(rem_hostname) == epeer);

  LOG_INFO(
      "eRPC Nexus: ENet socket connected to %s. Transmitting "
      "%zu queued SM requests.\n",
      rem_hostname.c_str(), epeer_data->client.tx_queue.size());

  // Transmit work items queued while waiting for connection
  assert(epeer_data->client.tx_queue.size() > 0);
  for (const SmWorkItem &wi : epeer_data->client.tx_queue) {
    sm_thread_tx_one(wi, epeer);
  }
  epeer_data->client.tx_queue.clear();
}

template <class TTr>
void Nexus<TTr>::sm_thread_on_enet_disconnect(SmThreadCtx &ctx, ENetEvent *ev) {
  assert(ev != nullptr);

  ENetPeer *epeer = ev->peer;  // Freed by ENet
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  if (epeer_data->is_server()) return;  // XXX: Server disconnect actions

  // If we're here, this is a client mode peer, so we have mappings
  uint32_t rem_ip = epeer->address.host;
  std::string rem_hostname = ctx.ip_map.at(rem_ip);
  assert(ctx.name_map.at(rem_hostname) == epeer);

  if (!epeer_data->client.connected) {
    // This peer didn't ever connect successfully, so try again. Carry over the
    // ENet peer data from the previous peer to the new peer.
    LOG_INFO("eRPC Nexus: ENet failed to connect peer to %s. Reconnecting.\n",
             rem_hostname.c_str());

    ENetAddress rem_address;
    rem_address.port = ctx.mgmt_udp_port;
    int ret = enet_address_set_host(&rem_address, rem_hostname.c_str());
    rt_assert(ret == 0, "ENet failed to resolve " + rem_hostname);

    ENetPeer *new_epeer = enet_host_connect(ctx.enet_host, &rem_address, 0, 0);
    rt_assert(new_epeer != nullptr, "ENet connect failed to " + rem_hostname);

    new_epeer->data = epeer->data;
    ctx.name_map.at(rem_hostname) = new_epeer;
  } else {
    LOG_INFO(
        "eRPC Nexus: ENet socket disconnected from %s. Not reconnecting.\n",
        rem_hostname.c_str());

    // XXX: Do something with outstanding SM requests on this peer (it could
    // be a session connect request). There may be requests from many sessions.

    // Remove from mappings and free memory
    ctx.ip_map.erase(rem_ip);
    ctx.name_map.erase(rem_hostname);
    delete static_cast<SmENetPeerData *>(epeer->data);
    epeer->data = nullptr;
  }

  return;
}

template <class TTr>
void Nexus<TTr>::sm_thread_on_enet_receive(SmThreadCtx &ctx, ENetEvent *ev) {
  assert(ev != nullptr && ev->peer != nullptr);
  assert(ev->peer->data != nullptr);

  // Copy the session management packet
  assert(ev->packet->dataLength == sizeof(SmPkt));
  SmPkt sm_pkt;
  memcpy(static_cast<void *>(&sm_pkt), ev->packet->data, sizeof(SmPkt));
  enet_packet_destroy(ev->packet);

  std::string rem_hostname = sm_pkt.get_remote_hostname();
  bool mappings_exist = ctx.name_map.count(rem_hostname) > 0;

  ENetPeer *epeer = ev->peer;

  // If mappings exist, check them
  if (mappings_exist) {
    assert(ctx.name_map.at(rem_hostname) == epeer);
    assert(ctx.ip_map.at(epeer->address.host) == rem_hostname);
  }

  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  if (epeer_data->is_server()) {
    assert(sm_pkt.is_req());

    // We don't have mappings for server peers initially
    if (!mappings_exist) {
      assert(!epeer_data->server.mappings_installed);

      ctx.name_map[rem_hostname] = epeer;
      ctx.ip_map[epeer->address.host] = rem_hostname;
      epeer_data->server.mappings_installed = true;
    }
  } else {
    assert(sm_pkt.is_resp());
    assert(mappings_exist);
  }

  LOG_INFO("eRPC Nexus: Received SM packet (type %s, sender %s).\n",
           sm_pkt_type_str(sm_pkt.pkt_type).c_str(), rem_hostname.c_str());

  // Handle reset peer request here, since it's not passed to any Rpc
  if (sm_pkt.pkt_type == SmPktType::kFaultResetPeerReq) {
    LOG_WARN(
        "eRPC Nexus: Received reset-remote-peer fault from Rpc [%s, %u]. "
        "Forcefully resetting ENet peer.\n",
        sm_pkt.client.hostname, sm_pkt.client.rpc_id);
    enet_peer_reset(epeer);  // XXX: Do we need to remove from maps?
    return;
  }

  uint8_t target_rpc_id =
      sm_pkt.is_req() ? sm_pkt.server.rpc_id : sm_pkt.client.rpc_id;

  // Lock the Nexus to prev Rpc registration while we lookup the hook
  ctx.nexus_lock->lock();
  Hook *target_hook = const_cast<Hook *>(ctx.reg_hooks_arr[target_rpc_id]);

  if (target_hook == nullptr) {
    // We don't have an Rpc object for the target Rpc. If sm_pkt is a request,
    // send a response if possible. Ignore if sm_pkt is a response.
    if (sm_pkt.is_req()) {
      // We don't handle this error for fault-injection session management reqs
      assert(sm_pkt_type_req_has_resp(sm_pkt.pkt_type));

      LOG_WARN(
          "eRPC Nexus: Received session management request for invalid "
          "Rpc %u from Rpc [%s, %u]. Sending response.\n",
          target_rpc_id, sm_pkt.client.hostname, sm_pkt.client.rpc_id);

      sm_pkt.pkt_type = sm_pkt_type_req_to_resp(sm_pkt.pkt_type);
      sm_pkt.err_type = SmErrType::kInvalidRemoteRpcId;

      SmWorkItem fake_wi(kInvalidRpcId, sm_pkt);
      sm_thread_tx_one(fake_wi, epeer);
    } else {
      LOG_WARN(
          "eRPC Nexus: Received session management response for invalid "
          "Rpc %u from Rpc [%s, %u]. Ignoring.\n",
          target_rpc_id, sm_pkt.client.hostname, sm_pkt.client.rpc_id);
    }

    ctx.nexus_lock->unlock();
    return;
  }

  // Submit a work item to the target Rpc
  SmWorkItem wi(target_rpc_id, sm_pkt);
  target_hook->sm_rx_list.unlocked_push_back(wi);

  ctx.nexus_lock->unlock();
  return;
}

template <class TTr>
void Nexus<TTr>::sm_thread_rx(SmThreadCtx &ctx) {
  ENetEvent ev;
  int ret = enet_host_service(ctx.enet_host, &ev, kSmThreadEventLoopMs);
  assert(ret >= 0);

  if (ret > 0) {
    // Process the ev
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT:
        sm_thread_on_enet_connect(ctx, &ev);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        sm_thread_on_enet_disconnect(ctx, &ev);
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        sm_thread_on_enet_receive(ctx, &ev);
        break;
      case ENET_EVENT_TYPE_NONE:
        throw std::runtime_error("eRPC Nexus: Unknown ENet ev type.\n");
    }
  }
}

template <class TTr>
void Nexus<TTr>::sm_thread_tx_one(const SmWorkItem &wi, ENetPeer *epeer) {
  assert(epeer != nullptr && epeer->data != nullptr);

  // This copies the packet payload
  ENetPacket *epkt =
      enet_packet_create(&wi.sm_pkt, sizeof(SmPkt), ENET_PACKET_FLAG_RELIABLE);
  rt_assert(epkt != nullptr, "Failed to create ENet packet.");

  rt_assert(enet_peer_send(epeer, 0, epkt) == 0, "enet_peer_send() failed");
}

template <class TTr>
void Nexus<TTr>::sm_thread_process_tx_queue(SmThreadCtx &ctx) {
  if (ctx.sm_tx_list->size == 0) return;

  ctx.sm_tx_list->lock();

  for (const SmWorkItem &wi : ctx.sm_tx_list->list) {
    assert(!wi.is_reset());  // Rpc threads cannot queue reset work items

    const SmPkt &sm_pkt = wi.sm_pkt;
    std::string rem_hostname = sm_pkt.get_remote_hostname();

    bool peer_exists = ctx.name_map.count(rem_hostname) > 0;
    if (sm_pkt.is_resp() && !peer_exists) {
      LOG_WARN(
          "eRPC Nexus: No epeer for SM response to Rpc [%s, %u]. Dropping.\n",
          sm_pkt.client.hostname, sm_pkt.client.rpc_id);
      continue;
    }

    if (peer_exists) {
      ENetPeer *epeer = ctx.name_map[rem_hostname];
      auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);

      if (epeer_data->is_client() && !epeer_data->client.connected) {
        assert(sm_pkt.is_req());
        epeer_data->client.tx_queue.push_back(wi);  // Transmit later on connect
      } else {
        sm_thread_tx_one(wi, epeer);
      }
    } else {
      assert(sm_pkt.is_req());  // We initiate peer creation from only clients

      ENetAddress rem_address;
      rem_address.port = ctx.mgmt_udp_port;
      if (enet_address_set_host(&rem_address, rem_hostname.c_str()) != 0) {
        throw std::runtime_error("ENet failed to resolve " + rem_hostname);
      }

      ENetPeer *epeer = enet_host_connect(ctx.enet_host, &rem_address, 0, 0);
      rt_assert(epeer != nullptr, "ENet connect failed to " + rem_hostname);

      enet_peer_timeout(epeer, 32, 30, 500);  // Reduce timeout

      // Add the peer to mappings to avoid creating a duplicate peer later
      ctx.name_map[rem_hostname] = epeer;
      ctx.ip_map[rem_address.host] = rem_hostname;

      epeer->data = new SmENetPeerData(SmENetPeerMode::kClient);
      auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
      epeer_data->client.tx_queue.push_back(wi);
    }
  }

  ctx.sm_tx_list->locked_clear();
  ctx.sm_tx_list->unlock();
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
    sm_thread_process_tx_queue(ctx);
    sm_thread_rx(ctx);
  }

  LOG_INFO("eRPC Nexus: Session management thread exiting.\n");

  // ENet cleanup
  enet_host_destroy(ctx.enet_host);
  enet_deinitialize();

  return;
}

}  // End ERpc
