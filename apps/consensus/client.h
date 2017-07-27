/**
 * @file client.h
 * @brief The client for the replicated service
 */

#include "consensus.h"
#include "util/latency.h"

#ifndef CLIENT_H
#define CLIENT_H

// Request tag, which doubles up as the request descriptor for the request queue
union client_req_tag_t {
  struct {
    uint64_t session_idx : 32;  // Index into context's session_num array
    uint64_t msgbuf_idx : 32;   // Index into context's req_msgbuf array
  };

  size_t _tag;

  client_req_tag_t(uint64_t session_idx, uint64_t msgbuf_idx)
      : session_idx(session_idx), msgbuf_idx(msgbuf_idx) {}
  client_req_tag_t(size_t _tag) : _tag(_tag) {}
  client_req_tag_t() : _tag(0) {}
};
static_assert(sizeof(client_req_tag_t) == sizeof(size_t), "");

// Per-thread application context
class AppContext {
 public:
  ERpc::Rpc<ERpc::IBTransport> *rpc = nullptr;
  ERpc::TmpStat *tmp_stat = nullptr;
  ERpc::Latency latency;

  std::vector<int> session_num_vec;

  // Index in session_num_vec that represents a self connection, *if it exists*
  size_t self_session_idx = std::numeric_limits<size_t>::max();

  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses
  struct timespec tput_t0;  // Start time for throughput measurement
  ERpc::FastRand fastrand;

  size_t stat_rx_bytes_tot = 0;  // Total bytes received
  size_t stat_tx_bytes_tot = 0;  // Total bytes transmitted

  std::vector<size_t> stat_req_vec;  // Number of requests sent on a session
  std::vector<client_req_tag_t> req_vec;  // Request queue

  uint64_t req_ts[kClientMaxConcurrency];  // Per-request timestamps
  ERpc::MsgBuffer req_msgbuf[kClientMaxConcurrency];
  ERpc::MsgBuffer resp_msgbuf[kClientMaxConcurrency];

  ~AppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

void client_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {}

#endif
