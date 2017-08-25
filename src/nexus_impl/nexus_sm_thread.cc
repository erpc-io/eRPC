#include "nexus.h"

namespace ERpc {

/// Amount of time to block for ENet evs
static constexpr size_t kSmThreadEventLoopMs = 20;

void Nexus::sm_thread_on_enet_connect_server(SmThreadCtx &, ENetEvent &ev) {
  ENetPeer *epeer = ev.peer;
  assert(epeer->data == nullptr);

  LOG_INFO("eRPC Nexus: ENet server peer (uninitialized) connected.\n");

  // We'll initialize peer data fields later on receiving an SM packet
  epeer->data = new SmENetPeerData(SmENetPeerMode::kServer);
}

void Nexus::sm_thread_on_enet_connect_client(SmThreadCtx &ctx, ENetEvent &ev) {
  _unused(ctx);
  ENetPeer *epeer = ev.peer;
  assert(epeer->data != nullptr);

  // If we're here, this is a client-mode ENet peer
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  assert(epeer_data->is_client() && !epeer_data->client.connected);
  epeer_data->client.connected = true;

  std::string rem_hostname = epeer_data->rem_hostname;
  assert(ctx.client_map.at(rem_hostname) == epeer);

  LOG_INFO(
      "eRPC Nexus: ENet client peer connected to %s. Transmitting "
      "%zu queued SM requests.\n",
      rem_hostname.c_str(), epeer_data->client.tx_queue.size());

  assert(epeer_data->client.tx_queue.size() > 0);
  for (const SmWorkItem &wi : epeer_data->client.tx_queue) {
    sm_thread_enet_send_one(wi, epeer);
  }
  epeer_data->client.tx_queue.clear();
}

void Nexus::sm_thread_broadcast_reset(SmThreadCtx &ctx,
                                      std::string rem_hostname) {
  SmWorkItem reset_wi(rem_hostname);

  // Lock the Nexus to prevent Rpc registration while we lookup live hooks
  ctx.nexus_lock->lock();
  for (size_t rpc_id = 0; rpc_id <= kMaxRpcId; rpc_id++) {
    if (ctx.reg_hooks_arr[rpc_id] != nullptr) {
      Hook *hook = const_cast<Hook *>(ctx.reg_hooks_arr[rpc_id]);
      hook->sm_rx_queue.unlocked_push(reset_wi);
    }
  }
  ctx.nexus_lock->unlock();
}

void Nexus::sm_thread_on_enet_disconnect_server(SmThreadCtx &ctx,
                                                ENetEvent &ev) {
  ENetPeer *epeer = ev.peer;  // epeer's memory is re-used by ENet

  // If we never received an SM packet, there's nothing to reset
  if (epeer->data == nullptr) {
    LOG_INFO("eRPC Nexus: ENet server peer (uninitialized) disconnected.\n");
    return;
  }

  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  std::string rem_hostname = epeer_data->rem_hostname;
  assert(ctx.server_map.at(rem_hostname) == epeer);

  // Remove from map and free memory
  ctx.server_map.erase(rem_hostname);
  delete static_cast<SmENetPeerData *>(epeer->data);
  epeer->data = nullptr;

  LOG_INFO(
      "eRPC Nexus: ENet server peer to %s disconnected. "
      "Broadcasting resets to local Rpcs.\n",
      rem_hostname.c_str());

  sm_thread_broadcast_reset(ctx, rem_hostname);
}

void Nexus::sm_thread_on_enet_disconnect_client(SmThreadCtx &ctx,
                                                ENetEvent &ev) {
  ENetPeer *epeer = ev.peer;  // epeer's memory is re-used by ENet
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
  assert(epeer_data->is_client());

  std::string rem_hostname = epeer_data->rem_hostname;
  assert(ctx.client_map.at(rem_hostname) == epeer);

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
    ctx.client_map.at(rem_hostname) = new_epeer;
  } else {
    // Remove from map and free memory
    ctx.client_map.erase(rem_hostname);
    delete static_cast<SmENetPeerData *>(epeer->data);
    epeer->data = nullptr;

    LOG_INFO(
        "eRPC Nexus: ENet client peer to %s disconnected. "
        "Broadcasting resets to local Rpcs.\n",
        rem_hostname.c_str());

    sm_thread_broadcast_reset(ctx, rem_hostname);
  }

  return;
}

SmPkt Nexus::sm_thread_pull_sm_pkt(ENetEvent &ev) {
  assert(ev.packet->dataLength == sizeof(SmPkt));
  SmPkt sm_pkt;
  memcpy(static_cast<void *>(&sm_pkt), ev.packet->data, sizeof(SmPkt));
  enet_packet_destroy(ev.packet);
  return sm_pkt;
}

void Nexus::sm_thread_on_enet_receive_server(SmThreadCtx &ctx, ENetEvent &ev) {
  assert(ev.peer != nullptr && ev.peer->data != nullptr);
  ENetPeer *epeer = ev.peer;
  auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);

  SmPkt sm_pkt = sm_thread_pull_sm_pkt(ev);
  assert(sm_pkt.is_req());
  const std::string &rem_hostname = sm_pkt.client.hostname;

  if (!epeer_data->server.initialized) {
    // We cannot have two server-mode peers to the same remote hostname
    assert(ctx.server_map.count(rem_hostname) == 0);
    ctx.server_map[rem_hostname] = epeer;

    epeer_data->rem_hostname = rem_hostname;
    epeer_data->server.initialized = true;
  }

  assert(ctx.server_map.at(rem_hostname) == epeer);

  LOG_INFO("eRPC Nexus: Received SM packet (type %s) from %s.\n",
           sm_pkt_type_str(sm_pkt.pkt_type).c_str(),
           sm_pkt.client.name().c_str());

  // Handle reset peer request here, since it's not passed to any Rpc
  if (sm_pkt.pkt_type == SmPktType::kFaultResetPeerReq) {
    LOG_WARN(
        "eRPC Nexus: Received reset-remote-peer fault from %s. "
        "Forcefully resetting ENet peer.\n",
        sm_pkt.client.name().c_str());
    enet_peer_reset(epeer);  // XXX: Do we need to remove from maps?
    return;
  }

  uint8_t target_rpc_id = sm_pkt.server.rpc_id;

  // Lock the Nexus to prevent Rpc registration while we lookup the hook
  ctx.nexus_lock->lock();
  Hook *target_hook = const_cast<Hook *>(ctx.reg_hooks_arr[target_rpc_id]);

  if (target_hook != nullptr) {
    target_hook->sm_rx_queue.unlocked_push(SmWorkItem(target_rpc_id, sm_pkt));
  } else {
    // We don't have an Rpc object for the target Rpc. Send resp if possible.
    assert(sm_pkt_type_req_has_resp(sm_pkt.pkt_type));

    LOG_WARN(
        "eRPC Nexus: Received session management request for invalid "
        "Rpc %u from %s. Sending response.\n",
        target_rpc_id, sm_pkt.client.name().c_str());

    sm_pkt.pkt_type = sm_pkt_type_req_to_resp(sm_pkt.pkt_type);
    sm_pkt.err_type = SmErrType::kInvalidRemoteRpcId;

    SmWorkItem fake_wi(kInvalidRpcId, sm_pkt);
    sm_thread_enet_send_one(fake_wi, epeer);
  }

  ctx.nexus_lock->unlock();
  return;
}

void Nexus::sm_thread_on_enet_receive_client(SmThreadCtx &ctx, ENetEvent &ev) {
  assert(ev.peer != nullptr && ev.peer->data != nullptr);

  SmPkt sm_pkt = sm_thread_pull_sm_pkt(ev);
  assert(sm_pkt.is_resp());

  const std::string &rem_hostname = sm_pkt.server.hostname;
  assert(ctx.client_map.at(rem_hostname) == ev.peer);

  LOG_INFO("eRPC Nexus: Received SM packet (type %s) from %s.\n",
           sm_pkt_type_str(sm_pkt.pkt_type).c_str(),
           sm_pkt.server.name().c_str());

  uint8_t target_rpc_id = sm_pkt.client.rpc_id;

  // Lock the Nexus to prevent Rpc registration while we lookup the hook
  ctx.nexus_lock->lock();
  Hook *target_hook = const_cast<Hook *>(ctx.reg_hooks_arr[target_rpc_id]);

  if (target_hook != nullptr) {
    target_hook->sm_rx_queue.unlocked_push(SmWorkItem(target_rpc_id, sm_pkt));
  } else {
    LOG_WARN(
        "eRPC Nexus: Received session management response for invalid "
        "Rpc %u from %s. Ignoring.\n",
        target_rpc_id, sm_pkt.client.name().c_str());
  }

  ctx.nexus_lock->unlock();
  return;
}

void Nexus::sm_thread_rx(SmThreadCtx &ctx) {
  ENetEvent ev;
  int ret = enet_host_service(ctx.enet_host, &ev, kSmThreadEventLoopMs);
  assert(ret >= 0);
  if (ret == 0) return;

  // Is the event for a server-mode peer?
  bool is_server = ev.peer->data == nullptr ||
                   static_cast<SmENetPeerData *>(ev.peer->data)->is_server();

  auto on_connect = is_server ? sm_thread_on_enet_connect_server
                              : sm_thread_on_enet_connect_client;

  auto on_disconnect = is_server ? sm_thread_on_enet_disconnect_server
                                 : sm_thread_on_enet_disconnect_client;

  auto on_receive = is_server ? sm_thread_on_enet_receive_server
                              : sm_thread_on_enet_receive_client;

  switch (ev.type) {
    case ENET_EVENT_TYPE_CONNECT:
      on_connect(ctx, ev);
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      on_disconnect(ctx, ev);
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      on_receive(ctx, ev);
      break;
    default:
      throw std::runtime_error("Unknown ENet event type");
  }
}

void Nexus::sm_thread_enet_send_one(const SmWorkItem &wi, ENetPeer *epeer) {
  assert(epeer != nullptr && epeer->data != nullptr);

  // This copies the packet payload
  ENetPacket *epkt =
      enet_packet_create(&wi.sm_pkt, sizeof(SmPkt), ENET_PACKET_FLAG_RELIABLE);
  rt_assert(epkt != nullptr, "Failed to create ENet packet.");

  rt_assert(enet_peer_send(epeer, 0, epkt) == 0, "enet_peer_send() failed");
}

// Process a (Rpc thread-enqueued) work item containing an SM request
void Nexus::sm_thread_process_tx_queue_req(SmThreadCtx &ctx,
                                           const SmWorkItem &wi) {
  const SmPkt &sm_pkt = wi.sm_pkt;
  assert(sm_pkt.is_req());

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Nexus: Trying to send SM packet (type %s, dest %s). Issue:",
          sm_pkt_type_str(sm_pkt.pkt_type).c_str(),
          sm_pkt.server.name().c_str());

  const std::string &rem_hostname = sm_pkt.server.hostname;
  bool peer_exists = ctx.client_map.count(rem_hostname) > 0;

  if (peer_exists) {
    ENetPeer *epeer = ctx.client_map.at(rem_hostname);
    auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);

    if (!epeer_data->client.connected) {
      LOG_INFO("%s: ENet peer not connected yet. Queueing.\n", issue_msg);
      epeer_data->client.tx_queue.push_back(wi);  // Transmit later on connect
    } else {
      LOG_INFO("%s: None. Sending now.\n", issue_msg);
      sm_thread_enet_send_one(wi, epeer);
    }
  } else {
    LOG_INFO("%s: ENet peer not created yet. Creating now.\n", issue_msg);

    ENetAddress rem_address;
    rem_address.port = ctx.mgmt_udp_port;
    if (enet_address_set_host(&rem_address, rem_hostname.c_str()) != 0) {
      throw std::runtime_error("ENet failed to resolve " + rem_hostname);
    }

    ENetPeer *epeer = enet_host_connect(ctx.enet_host, &rem_address, 0, 0);
    rt_assert(epeer != nullptr, "ENet connect failed to " + rem_hostname);

    enet_peer_timeout(epeer, 32, 30, 500);  // Reduce timeout

    epeer->data = new SmENetPeerData(SmENetPeerMode::kClient);
    auto *epeer_data = static_cast<SmENetPeerData *>(epeer->data);
    epeer_data->rem_hostname = rem_hostname;
    epeer_data->client.tx_queue.push_back(wi);

    ctx.client_map[rem_hostname] = epeer;
  }
}

// Process a (Rpc thread-enqueued) work item containing an SM response
void Nexus::sm_thread_process_tx_queue_resp(SmThreadCtx &ctx,
                                            const SmWorkItem &wi) {
  const SmPkt &sm_pkt = wi.sm_pkt;
  assert(sm_pkt.is_resp());
  const std::string &rem_hostname = sm_pkt.client.hostname;

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Nexus: Trying to send SM packet (type %s, dest %s). Issue:",
          sm_pkt_type_str(sm_pkt.pkt_type).c_str(),
          sm_pkt.client.name().c_str());

  bool peer_exists = ctx.server_map.count(rem_hostname) > 0;

  if (!peer_exists) {
    LOG_WARN("%s: No epeer. Dropping.\n", issue_msg);
    return;
  }

  LOG_INFO("%s: None. Sending response.\n", issue_msg);
  sm_thread_enet_send_one(wi, ctx.server_map.at(rem_hostname));
}

void Nexus::sm_thread_process_tx_queue(SmThreadCtx &ctx) {
  while (ctx.sm_tx_queue->size > 0) {
    const SmWorkItem wi = ctx.sm_tx_queue->unlocked_pop();
    assert(!wi.is_reset());  // Rpc threads cannot queue reset work items

    if (wi.sm_pkt.is_req()) {
      sm_thread_process_tx_queue_req(ctx, wi);
    } else {
      sm_thread_process_tx_queue_resp(ctx, wi);
    }
  }
}

void Nexus::sm_thread_func(SmThreadCtx ctx) {
  // Create an ENet host that remote nodes can connect to
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
