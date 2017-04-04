#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "nexus.h"

namespace ERpc {

template <class TTr>
void Nexus<TTr>::sm_thread_func(volatile bool *sm_kill_switch,
                                volatile Hook **reg_hooks_arr,
                                std::mutex *nexus_lock,
                                const udp_config_t *udp_config) {
  // Create a UDP socket.
  // AF_INET = IPv4, SOCK_DGRAM = datagrams, IPPROTO_UDP = datagrams over UDP.
  // Returns a file descriptor.
  int sm_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sm_sock_fd < 0) {
    throw std::runtime_error("eRPC Nexus: Error opening datagram socket.");
  }

  // Bind the socket to accept packets destined to any IP interface of this
  // machine (INADDR_ANY), and to mgmt_udp_port.
  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(udp_config->mgmt_udp_port);

  if (bind(sm_sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) <
      0) {
    throw std::runtime_error("eRPC Nexus: Error binding datagram socket.");
  }

  // Set socket timeout to 100 ms, so we can kill ourselves when the Nexus
  // is destroyed
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  if (setsockopt(sm_sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    throw std::runtime_error("eRPC Nexus: Error setting SM socket timeout.");
  }

  while (*sm_kill_switch == false) {
    uint32_t addr_len = sizeof(struct sockaddr_in);  // Value-result
    struct sockaddr_in their_addr;  // Sender's address information goes here
    SessionMgmtPkt *sm_pkt = new SessionMgmtPkt();  // Need new: passed to Rpc
    int flags = 0;

    // Receive a packet from the socket. We're guaranteed to get exactly one.
    ssize_t recv_bytes =
        recvfrom(sm_sock_fd, (void *)sm_pkt, sizeof(*sm_pkt), flags,
                 (struct sockaddr *)&their_addr, &addr_len);

    // The recvfrom() call can fail only due to timeouts
    if (recv_bytes == -1) {
      if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
        throw std::runtime_error("eRPC Nexus: SM socket recvfrom() error.\n");
      } else {
        continue;  // Try to recv again, or die if the Nexus is destroyed
      }
    }

    if (recv_bytes != (ssize_t)sizeof(*sm_pkt)) {
      erpc_dprintf(
          "eRPC Nexus: FATAL. Received unexpected data size (%zd) from "
          "session management socket. Expected = %zu.\n",
          recv_bytes, sizeof(*sm_pkt));
      assert(false);
      exit(-1);
    }

    if (!session_mgmt_pkt_type_is_valid(sm_pkt->pkt_type)) {
      erpc_dprintf(
          "eRPC Nexus: FATAL. Received session management packet of "
          "unexpected type %d.\n",
          static_cast<int>(sm_pkt->pkt_type));
      assert(false);
      exit(-1);
    }

    uint8_t target_app_tid;  // TID of the Rpc that should handle this packet
    const char *source_hostname;
    uint8_t source_app_tid;  // Debug-only
    _unused(source_app_tid);

    bool is_sm_req = session_mgmt_pkt_type_is_req(sm_pkt->pkt_type);

    if (is_sm_req) {
      target_app_tid = sm_pkt->server.app_tid;
      source_app_tid = sm_pkt->client.app_tid;
      source_hostname = sm_pkt->client.hostname;
    } else {
      target_app_tid = sm_pkt->client.app_tid;
      source_app_tid = sm_pkt->server.app_tid;
      source_hostname = sm_pkt->server.hostname;
    }

    // Find the registered Rpc that has this TID
    Hook *target_hook = (Hook *)(reg_hooks_arr[target_app_tid]);

    // Lock the Nexus to prevent Rpc registration while we lookup the hook
    nexus_lock->lock();
    if (target_hook == nullptr) {
      // We don't have an Rpc object for target_app_tid
      if (is_sm_req) {
        // If it's a request, we must send a response
        erpc_dprintf(
            "eRPC Nexus: Received session mgmt request for invalid Rpc %u "
            "from Rpc [%s, %u]. Sending response.\n",
            target_app_tid, source_hostname, source_app_tid);

        sm_pkt->send_resp_mut(SessionMgmtErrType::kInvalidRemoteAppTid,
                              udp_config);
      } else {
        // If it's a response, we can ignore it
        erpc_dprintf(
            "eRPC Nexus: Received session management resp for invalid Rpc %u "
            "from Rpc [%s, %u]. Ignoring.\n",
            target_app_tid, source_hostname, source_app_tid);
      }

      nexus_lock->unlock();
      delete sm_pkt;
      return;
    }

    // Add the packet to the target Rpc's session management packet list
    target_hook->sm_pkt_list.unlocked_push_back(sm_pkt);

    nexus_lock->unlock();
  }

  erpc_dprintf_noargs("eRPC Nexus: Session management thread exiting.\n");

  // Close the session management socket
  int ret = close(sm_sock_fd);
  if (ret != 0) {
    erpc_dprintf_noargs(
        "eRPC Nexus: Failed to close session management socket. Ignoring.\n");
  }

  return;
}

}  // End ERpc
