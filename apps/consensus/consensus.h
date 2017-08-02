#ifndef CONSENSUS_H
#define CONSENSUS_H

extern "C" {
#include <raft/raft.h>
}

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>

#include "../apps_common.h"
#include "cityhash/city.h"
#include "rpc.h"
#include "util/latency.h"

// Debug/measurement
static constexpr bool kAppCollectTimeEntries = false;
static constexpr bool kAppVerbose = false;
static constexpr bool kAppEnableRaftConsoleLog = false;  // Non-null console log

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kIPStrLen = 12;
static constexpr size_t kClientMaxConcurrency = 32;

// We run FLAGS_num_machines in the cluster, of which the first
// FLAGS_num_raft_servers are Raft servers, and the remaining machines are Raft
// clients.
DEFINE_uint64(num_raft_servers, 0,
              "Number of Raft servers (i.e., non-client machines)");
static bool validate_num_raft_servers(const char *, uint64_t num_raft_servers) {
  return num_raft_servers > 0 && num_raft_servers % 2 == 1;
}
DEFINE_validator(num_raft_servers, &validate_num_raft_servers);

// Return true iff this machine is a Raft server (leader or follower)
bool is_raft_server() { return FLAGS_machine_id < FLAGS_num_raft_servers; }

/// The eRPC request types
enum class ReqType : uint8_t {
  kRequestVote = 3,  // Raft requestvote RPC
  kAppendEntries,    // Raft appendentries RPC
  kClientReq         // Client-to-server Rpc
};

class AppContext;  // Forward declaration

// Peer-peer or client-peer connection
struct connection_t {
  int session_num = -1;  // ERpc session number
  size_t session_idx = std::numeric_limits<size_t>::max();  // Index in vector
  AppContext *c;  // Back link to AppContext
};

// Tag for requests sent to Raft peers
struct raft_req_tag_t {
  ERpc::MsgBuffer req_msgbuf;
  ERpc::MsgBuffer resp_msgbuf;
  raft_node_t *node;  // The Raft node to which req was sent (for servers only)
};

// Info about client request(s) saved at a leader for the nested Rpc
struct leader_saveinfo_t {
  bool in_use = false;          // Leader has an ongoing commit request
  ERpc::ReqHandle *req_handle;  // This could be a vector if we do batching
  msg_entry_response_t msg_entry_response;
  size_t counter;
};

// Comments describe the common-case usage
enum class TimeEntryType {
  kClientReq,   // Client request received by leader
  kSendAeReq,   // Leader sends appendentry request
  kRecvAeReq,   // Follower receives appendentry request
  kSendAeResp,  // Follower sends appendentry response
  kRecvAeResp,  // Leader receives appendentry response
  kCommitted    // Entry committed at leader
};

class TimeEntry {
 public:
  TimeEntryType time_entry_type;
  size_t tsc;

  TimeEntry() {}
  TimeEntry(TimeEntryType t, size_t tsc) : time_entry_type(t), tsc(tsc) {}

  std::string to_string(size_t base_tsc, double freq_ghz) const {
    std::string ret;

    switch (time_entry_type) {
      case TimeEntryType::kClientReq:
        ret = "client_req";
        break;
      case TimeEntryType::kSendAeReq:
        ret = "send_appendentries_req";
        break;
      case TimeEntryType::kRecvAeReq:
        ret = "recv_appendentries_req";
        break;
      case TimeEntryType::kSendAeResp:
        ret = "send_appendentries_resp";
        break;
      case TimeEntryType::kRecvAeResp:
        ret = "recv_appendentries_resp";
        break;
      case TimeEntryType::kCommitted:
        ret = "committed";
        break;
    }

    double usec = ERpc::to_usec(tsc - base_tsc, freq_ghz);
    ret += ": " + std::to_string(usec);
    return ret;
  }
};

// Context for both servers and clients
class AppContext {
 public:
  // Raft server members
  struct {
    int node_id = -1;  // This server's Raft node ID
    raft_server_t *raft = nullptr;
    std::vector<raft_entry_t> raft_log;  // The Raft log, vector is OK
    size_t raft_periodic_tsc;            // rdtsc timestamp
    leader_saveinfo_t leader_saveinfo;   // Info for the ongoing commit request
    std::vector<TimeEntry> time_entry_vec;

    size_t cur_counter = 0;
    ERpc::TscLatency commit_latency;  // Leader latency to commit an entry

    // Stats
    size_t stat_requestvote_enq_fail = 0;    // Failed to send requestvote req
    size_t stat_appendentries_enq_fail = 0;  // Failed to send appendentries req
  } server;

  // Consensus client members
  struct {
    size_t thread_id;
    size_t leader_idx;  // Client's view of the leader node's index in conn_vec
    size_t num_resps = 0;
    ERpc::MsgBuffer req_msgbuf;
    ERpc::MsgBuffer resp_msgbuf;
    ERpc::TscLatency req_latency;  // Request latency observed by client
  } client;

  // Common members
  std::vector<connection_t> conn_vec;
  ERpc::Rpc<ERpc::IBTransport> *rpc;
  ERpc::FastRand fast_rand;
  size_t num_sm_resps = 0;

  // Magic
  static constexpr size_t kAppContextMagic = 0x3185;
  volatile size_t magic = kAppContextMagic;  // Avoid optimizing check_magic()
  bool check_magic() const { return magic == kAppContextMagic; }
};

// Generate a deterministic, random-ish node ID from a machine's hostname
int get_raft_node_id_from_hostname(std::string hostname) {
  uint32_t hash = CityHash32(hostname.c_str(), hostname.length());
  return static_cast<int>(hash);
}

// ERpc sessiom management handler
void sm_handler(int session_num, ERpc::SmEventType sm_event_type,
                ERpc::SmErrType sm_err_type, void *_context) {
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);
  c->num_sm_resps++;

  if (sm_err_type != ERpc::SmErrType::kNoError) {
    throw std::runtime_error("Received SM response with error.");
  }

  if (!(sm_event_type == ERpc::SmEventType::kConnected ||
        sm_event_type == ERpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the ERpc session number - get the index in conn_vec
  size_t session_idx = c->conn_vec.size();
  for (size_t i = 0; i < c->conn_vec.size(); i++) {
    if (c->conn_vec[i].session_num == session_num) {
      session_idx = i;
    }
  }

  if (session_idx == c->conn_vec.size()) {
    throw std::runtime_error("SM callback for invalid session number.");
  }

  fprintf(stderr,
          "consensus: Rpc %u: Session number %d (index %zu) %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          sm_event_type == ERpc::SmEventType::kConnected ? "connected"
                                                         : "disconncted",
          c->rpc->sec_since_creation());
}

// Globals
std::unordered_map<int, std::string> node_id_to_name_map;

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

inline void call_raft_periodic(AppContext *c) {
  // raft_periodic() takes the number of msec elapsed since the last call. This
  // is done for timeouts which are > 100 msec, so this approximation is fine.
  size_t cur_tsc = ERpc::rdtsc();

  // Assume TSC freqency is around 2.8 GHz. 1 ms = 2.8 * 100,000 ticks.
  bool msec_elapsed = (ERpc::rdtsc() - c->server.raft_periodic_tsc > 2800000);

  if (msec_elapsed) {
    c->server.raft_periodic_tsc = cur_tsc;
    raft_periodic(c->server.raft, 1);
  } else {
    raft_periodic(c->server.raft, 0);
  }
}

#endif
