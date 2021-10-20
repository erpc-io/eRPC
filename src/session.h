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

/// The arguments to enqueue_request()
struct enq_req_args_t {
  int session_num_;
  uint8_t req_type_;
  MsgBuffer *req_msgbuf_;
  MsgBuffer *resp_msgbuf_;
  erpc_cont_func_t cont_func_;
  void *tag_;
  size_t cont_etid_;

  enq_req_args_t() {}
  enq_req_args_t(int session_num, uint8_t req_type, MsgBuffer *req_msgbuf,
                 MsgBuffer *resp_msgbuf, erpc_cont_func_t cont_func, void *tag,
                 size_t cont_etid)
      : session_num_(session_num),
        req_type_(req_type),
        req_msgbuf_(req_msgbuf),
        resp_msgbuf_(resp_msgbuf),
        cont_func_(cont_func),
        tag_(tag),
        cont_etid_(cont_etid) {}
};

/// The arguments to enqueue_response()
struct enq_resp_args_t {
  ReqHandle *req_handle_;
  MsgBuffer *resp_msgbuf_;

  enq_resp_args_t() {}
  enq_resp_args_t(ReqHandle *req_handle, MsgBuffer *resp_msgbuf)
      : req_handle_(req_handle), resp_msgbuf_(resp_msgbuf) {}
};

// Forward declaration for friendship
template <typename T>
class Rpc;

/// A one-to-one session class for all transports
class Session {
  friend class Rpc<CTransport>;
  friend class ReqHandle;

 public:
  enum class Role : int { kServer, kClient };

 private:
  Session(Role role, conn_req_uniq_token_t uniq_token, double freq_ghz,
          double link_bandwidth)
      : role_(role),
        uniq_token_(uniq_token),
        freq_ghz_(freq_ghz),
        link_bandwidth_(link_bandwidth) {
    remote_routing_info_ =
        is_client() ? &server_.routing_info_ : &client_.routing_info_;

    if (is_client())
      client_info_.cc_.timely_ = Timely(freq_ghz, link_bandwidth);

    // Arrange the free slot vector so that slots are popped in order
    for (size_t i = 0; i < kSessionReqWindow; i++) {
      // Initialize session slot with index = sslot_i
      const size_t sslot_i = (kSessionReqWindow - 1 - i);
      SSlot &sslot = sslot_arr_[sslot_i];

      // This buries all MsgBuffers
      memset(static_cast<void *>(&sslot), 0, sizeof(SSlot));

      sslot.session_ = this;
      sslot.is_client_ = is_client();
      sslot.index_ = sslot_i;
      sslot.cur_req_num_ = sslot_i;  // 1st req num = (+kSessionReqWindow)

      if (is_client()) {
        for (auto &x : sslot.client_info_.in_wheel_) x = false;
      } else {
        sslot.server_info_.req_type_ = kInvalidReqType;
      }

      client_info_.sslot_free_vec_.push_back(sslot_i);
    }
  }

  /// All session resources are freed by the owner Rpc
  ~Session() {}

  inline bool is_client() const { return role_ == Role::kClient; }
  inline bool is_server() const { return role_ == Role::kServer; }
  inline bool is_connected() const {
    return state_ == SessionState::kConnected;
  }

  /**
   * @brief Get the desired TX timestamp, and update TX timestamp tracking
   *
   * @param ref_tsc A recent TSC that to detect if we are lagging
   * @param pkt_size The size of the packet to transmit
   * @return The desired TX timestamp for this packet
   */
  inline size_t cc_getupdate_tx_tsc(size_t ref_tsc, size_t pkt_size) {
    double ns_delta = 1000000000 * (pkt_size / client_info_.cc_.timely_.rate_);
    double cycle_delta = ns_to_cycles(ns_delta, freq_ghz_);

    size_t desired_tx_tsc = client_info_.cc_.prev_desired_tx_tsc_ + cycle_delta;
    desired_tx_tsc = (std::max)(desired_tx_tsc, ref_tsc);

    client_info_.cc_.prev_desired_tx_tsc_ = desired_tx_tsc;

    return desired_tx_tsc;
  }

  /// Return true iff this session is uncongested
  inline bool is_uncongested() const {
    return client_info_.cc_.timely_.rate_ == link_bandwidth_;
  }

  /// Return the hostname of the remote endpoint for a connected session
  std::string get_remote_hostname() const {
    if (is_client()) return trim_hostname(server_.hostname_);
    return trim_hostname(client_.hostname_);
  }

  const Role role_;  ///< The role (server/client) of this session endpoint
  const conn_req_uniq_token_t uniq_token_;  ///< A cluster-wide unique token
  const double freq_ghz_;                   ///< TSC frequency
  const double link_bandwidth_;  ///< Link bandwidth in bytes per second
  SessionState state_;  ///< The management state of this session endpoint
  SessionEndpoint client_, server_;  ///< Read-only endpoint metadata

  std::array<SSlot, kSessionReqWindow> sslot_arr_;  ///< The session slots

  ///@{ Info saved for faster unconditional access
  Transport::routing_info_t *remote_routing_info_;
  uint16_t local_session_num_;
  uint16_t remote_session_num_;
  ///@}

  /// Information that is required only at the client endpoint
  struct {
    size_t credits_ = kSessionCredits;  ///< Currently available credits

    /// Free session slots. We could use sslot pointers, but indices are useful
    /// in request number calculation.
    FixedVector<size_t, kSessionReqWindow> sslot_free_vec_;

    /// Requests that spill over kSessionReqWindow are queued here
    std::queue<enq_req_args_t> enq_req_backlog_;

    size_t num_re_tx_ = 0;  ///< Number of retransmissions for this session

    // Congestion control
    struct {
      Timely timely_;
      size_t prev_desired_tx_tsc_;  ///< Desired TX timestamp of the last packet
    } cc_;

    size_t sm_req_ts_;  ///< Timestamp of the last session management request
  } client_info_;
};

}  // namespace erpc
