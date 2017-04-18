#include "nexus.h"

namespace ERpc {

static constexpr size_t kENetChannels = 1;

/// The session management thread blocks on ENet events for 20 ms
static constexpr size_t kSmThreadEventLoopMs = 20;

/// Timeout for establishing ENet connections
static constexpr size_t kENetConnectTimeoutMs = 50;

template <class TTr>
void Nexus<TTr>::sm_thread_rx(SmThreadCtx *ctx) {
  assert(ctx != nullptr);

  ENetEvent event;
  int ret = enet_host_service(ctx->enet_host, &event, kSmThreadEventLoopMs);
  assert(ret >= 0);

  if (ret > 0) {
    // Process the event
    ENetPeer *enet_peer = event.peer;
    uint32_t peer_ip = event.peer->address.host;

    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        if (enet_peer->data == nullptr) {
          // This is a server-mode peer, so we don't need to do anything
          break;
        }

        // If we're here, this is a client-mode session
        auto *peer_data = static_cast<SmPeerData *>(enet_peer->data);
        assert(!peer_data->connected);
        assert(peer_data->work_item_vec.size() > 0);

        peer_data->connected = true;
        erpc_dprintf(
            "eRPC Nexus: ENet socket connected to %s. Transmitting "
            "%zu queued SM requests.\n",
            peer_data->rem_hostname.c_str(), peer_data->work_item_vec.size());

        // Transmit the queued work items
        for (auto wi : peer_data->work_item_vec) {
          assert(wi.sm_pkt->is_req());
          sm_tx_work_item_and_free(&wi);
        }
        peer_data->work_item_vec.clear();

        break;
      }

      case ENET_EVENT_TYPE_RECEIVE: {
        assert(event.packet->dataLength == sizeof(SessionMgmtPkt));

        if (enet_peer->data != nullptr) {
          // This is an event for a client-mode session, so we have mappings
          assert(ctx->ip_map.count(peer_ip) > 0);
          assert(ctx->name_map[ctx->ip_map[peer_ip]] == enet_peer);
        }

        // Copy out the packet and free the ENet packet
        SessionMgmtPkt *sm_pkt = new SessionMgmtPkt();
        memcpy(static_cast<void *>(sm_pkt), event.packet->data,
               sizeof(SessionMgmtPkt));
        enet_packet_destroy(event.packet);

        assert(session_mgmt_pkt_type_is_valid(sm_pkt->pkt_type));

        bool is_sm_req = sm_pkt->is_req();
        uint8_t target_rpc_id =
            is_sm_req ? sm_pkt->server.rpc_id : sm_pkt->client.rpc_id;
        const char *source_hostname =
            is_sm_req ? sm_pkt->client.hostname : sm_pkt->server.hostname;
        uint8_t source_rpc_id =
            is_sm_req ? sm_pkt->client.rpc_id : sm_pkt->server.rpc_id;
        _unused(source_rpc_id);

        // Lock the Nexus to prevent Rpc registration while we lookup the hook
        ctx->nexus_lock->lock();
        Hook *target_hook =
            const_cast<Hook *>(ctx->reg_hooks_arr[target_rpc_id]);

        if (target_hook == nullptr) {
          // We don't have an Rpc object for the target Rpc
          if (is_sm_req) {
            // If it's a request, we must send a response. We don't have a
            // local Rpc ID, so we cannot use sm_tx_work_item_and_free().
            erpc_dprintf(
                "eRPC Nexus: Received session mgmt request for invalid Rpc "
                "%u from Rpc [%s, %u]. Sending response.\n",
                target_rpc_id, source_hostname, source_rpc_id);

            sm_pkt->pkt_type =
                session_mgmt_pkt_type_req_to_resp(sm_pkt->pkt_type);
            sm_pkt->err_type = SessionMgmtErrType::kInvalidRemoteRpcId;

            ENetPacket *packet = enet_packet_create(static_cast<void *>(sm_pkt),
                                                    sizeof(SessionMgmtPkt),
                                                    ENET_PACKET_FLAG_RELIABLE);

            enet_peer_send(enet_peer, 0, packet);
          } else {
            // If it's a response, we can ignore it
            erpc_dprintf(
                "eRPC Nexus: Received session management resp for invalid "
                "Rpc %u from Rpc [%s, %u]. Ignoring.\n",
                target_rpc_id, source_hostname, source_rpc_id);
          }

          delete sm_pkt;
          ctx->nexus_lock->unlock();
        } else {
          // Create the work item to submit to the Rpc thread. For session
          // mgmt requests, the Rpc thread uses the ENet peer for responding.
          if (is_sm_req) {
            SmWorkItem wi(target_rpc_id, sm_pkt, event.peer);
            target_hook->sm_rx_list.unlocked_push_back(wi);
          } else {
            SmWorkItem wi(target_rpc_id, sm_pkt, nullptr);
            target_hook->sm_rx_list.unlocked_push_back(wi);
          }

          // The target Rpc will free sm_pkt
          ctx->nexus_lock->unlock();
        }
        break;
      }

      case ENET_EVENT_TYPE_DISCONNECT: {
        if (enet_peer->data == nullptr) {
          // This is a server-mode peer, so we don't need to do anything
          break;
        }

        // If we're here, this is a client mode peer, so we have mappings
        assert(ctx->ip_map.count(peer_ip) > 0);
        std::string hostname = ctx->ip_map[peer_ip];
        assert(ctx->name_map[hostname] == enet_peer);

        erpc_dprintf("eRPC Nexus: ENet socket disconnected from %s.\n",
                     hostname.c_str());

        // XXX: Do something with outstanding SM requests on this peer. There
        // may be requests from many sessions.

        // Remove from mappings and free memory
        ctx->ip_map.erase(peer_ip);
        ctx->name_map.erase(hostname);
        delete static_cast<SmPeerData *>(enet_peer->data);
        enet_peer->data = nullptr;
        break;
      }

      default:
        throw std::runtime_error("eRPC Nexus: Unknown ENet event type.\n");
        break;
    }
  }
}

template <class TTr>
void Nexus<TTr>::sm_tx_work_item_and_free(SmWorkItem *wi) {
  assert(wi != nullptr);
  assert(wi->enet_peer != nullptr);
  assert(wi->sm_pkt != nullptr);

  // If the work item uses a client-mode peer, the peer must be connected
  if (wi->enet_peer->data != nullptr) {
    assert(static_cast<SmPeerData *>(wi->enet_peer->data)->connected);
  }

  // Create the packet to send
  ENetPacket *enet_pkt = enet_packet_create(wi->sm_pkt, sizeof(SessionMgmtPkt),
                                            ENET_PACKET_FLAG_RELIABLE);
  if (enet_pkt == nullptr) {
    throw std::runtime_error("eRPC Nexus: Failed to create ENet packet.");
  }
  delete wi->sm_pkt;

  if (enet_peer_send(wi->enet_peer, 0, enet_pkt) != 0) {
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
    assert(session_mgmt_pkt_type_is_valid(wi.sm_pkt->pkt_type));

    if (wi.sm_pkt->is_req()) {
      // Transmit a session management request
      assert(wi.enet_peer == nullptr);
      std::string rem_hostname = std::string(wi.sm_pkt->server.hostname);

      if (ctx->name_map.count(rem_hostname) != 0) {
        // We already have a client-mode ENet peer to this host
        ENetPeer *enet_peer = ctx->name_map[rem_hostname];
        assert(enet_peer != nullptr);

        wi.enet_peer = enet_peer;

        // Wait if the peer is not yet connected
        auto *peer_data = static_cast<SmPeerData *>(enet_peer->data);
        if (!peer_data->connected) {
          peer_data->work_item_vec.push_back(wi);
        } else {
          sm_tx_work_item_and_free(&wi);
        }
      } else {
        // We don't have a client-mode ENet peer to this host - create one
        ENetAddress rem_address;
        rem_address.port = ctx->mgmt_udp_port;
        if (enet_address_set_host(&rem_address, rem_hostname.c_str()) != 0) {
          throw std::runtime_error(
              "eRPC Nexus: ENet Failed to resolve address " + rem_hostname);
        }

        ENetPeer *enet_peer =
            enet_host_connect(ctx->enet_host, &rem_address, kENetChannels, 0);
        if (enet_peer == nullptr) {
          throw std::runtime_error("eRPC Nexus: Failed to connect ENet to " +
                                   rem_hostname);
        }

        wi.enet_peer = enet_peer;

        // Add the peer to mappings to avoid creating a duplicate peer
        ctx->name_map[rem_hostname] = enet_peer;
        ctx->ip_map[rem_address.host] = rem_hostname;

        // Save the work item - we'll transmit it when we get connected
        enet_peer->data = new SmPeerData();
        auto *peer_data = static_cast<SmPeerData *>(enet_peer->data);
        peer_data->rem_hostname = rem_hostname;
        peer_data->connected = false;

        peer_data->work_item_vec.push_back(wi);
      }
    } else {
      // Transmit a session management response
      assert(wi.enet_peer != nullptr);
      sm_tx_work_item_and_free(&wi);
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

  // This is not a busy loop, since sm_thread_rx() blocks for several ms.
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
