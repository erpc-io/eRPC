#pragma once

#include <limits>
#include <mutex>
#include <queue>

#include "cc/timely.h"
#include "cc/timing_wheel.h"
#include "common.h"
#include "msg_buffer.h"
#include "rpc_types.h"
#include "sm_types.h"
#include "sslot.h"
#include "util/buffer.h"
#include "util/fixed_vector.h"

namespace erpc {

// Forward declaration for friendship
template <typename T>
class Rpc;

/// A one-to-one session class for all transports
class Session {
  friend class Rpc<CTransport>;

 public:
  enum class Role : int { kServer, kClient };

 private:
  Session(Role role, conn_req_uniq_token_t uniq_token, double freq_ghz,
          double link_bandwidth)
      : role(role),
        uniq_token(uniq_token),
        freq_ghz(freq_ghz),
        link_bandwidth(link_bandwidth) {
    remote_routing_info =
        is_client() ? &server.routing_info : &client.routing_info;

    if (is_client()) client_info.cc.timely = Timely(freq_ghz, link_bandwidth);

    // Arrange the free slot vector so that slots are popped in order
    for (size_t i = 0; i < kSessionReqWindow; i++) {
      // Initialize session slot with index = sslot_i
      const size_t sslot_i = (kSessionReqWindow - 1 - i);
      SSlot &sslot = sslot_arr[sslot_i];

      // This buries all MsgBuffers
      memset(static_cast<void *>(&sslot), 0, sizeof(SSlot));

      sslot.prealloc_used = true;  // There's no user-allocated memory to free
      sslot.session = this;
      sslot.is_client = is_client();
      sslot.index = sslot_i;
      sslot.cur_req_num = sslot_i;  // 1st req num = (+kSessionReqWindow)

      if (is_client()) {
        for (auto &x : sslot.client_info.in_wheel) x = false;
        sslot.client_info.cont_etid = kInvalidBgETid;  // Continuations in fg
      } else {
        sslot.server_info.req_type = kInvalidReqType;
      }

      client_info.sslot_free_vec.push_back(sslot_i);
    }
  }

  /// All session resources are freed by the owner Rpc
  ~Session() {}

  inline bool is_client() const { return role == Role::kClient; }
  inline bool is_server() const { return role == Role::kServer; }
  inline bool is_connected() const { return state == SessionState::kConnected; }

  /**
   * @brief Get the desired TX timestamp, and update TX timestamp tracking
   *
   * @param ref_tsc A recent TSC that to detect if we are lagging
   * @param pkt_size The size of the packet to transmit
   * @return The desired TX timestamp for this packet
   */
  inline size_t cc_getupdate_tx_tsc(size_t ref_tsc, size_t pkt_size) {
    double ns_delta = 1000000000 * (pkt_size / client_info.cc.timely.rate);

    size_t &prev_desired_tx_tsc = client_info.cc.prev_desired_tx_tsc;
    prev_desired_tx_tsc += ns_to_cycles(ns_delta, freq_ghz);
    prev_desired_tx_tsc = std::max(ref_tsc, prev_desired_tx_tsc);
    return prev_desired_tx_tsc;
  }

  /// Return true iff this session is uncongested
  inline bool is_uncongested() const {
    return client_info.cc.timely.rate == link_bandwidth;
  }

  /// Return the hostname of the remote endpoint for a connected session
  std::string get_remote_hostname() const {
    if (is_client()) return trim_hostname(server.hostname);
    return trim_hostname(client.hostname);
  }

  const Role role;  ///< The role (server/client) of this session endpoint
  const conn_req_uniq_token_t uniq_token;  ///< A cluster-wide unique token
  const double freq_ghz;                   ///< TSC frequency
  const double link_bandwidth;  ///< Link bandwidth in bytes per second
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< Read-only endpoint metadata

  std::array<SSlot, kSessionReqWindow> sslot_arr;  ///< The session slots

  ///@{ Info saved for faster unconditional access
  Transport::RoutingInfo *remote_routing_info;
  uint16_t local_session_num;
  uint16_t remote_session_num;
  ///@}

  /// Information that is required only at the client endpoint
  struct {
    size_t credits = kSessionCredits;  ///< Currently available credits

    /// Free session slots. We could use sslot pointers, but indices are useful
    /// in request number calculation.
    FixedVector<size_t, kSessionReqWindow> sslot_free_vec;

    /// Requests that spill over kSessionReqWindow are queued here
    std::queue<enq_req_args_t> enq_req_backlog;

    size_t num_re_tx = 0;  ///< Number of retransmissions for this session

    // Congestion control
    struct {
      Timely timely;
      size_t prev_desired_tx_tsc;  ///< Desired TX timestamp of the last packet
    } cc;

    size_t sm_req_ts;  ///< Timestamp of the last session management request
  } client_info;
};

}  // namespace erpc
