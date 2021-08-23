/**
 * @file smr.h
 * @brief Common code for SMR client and server
 */

#pragma once

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <unordered_map>

#include "../apps_common.h"
#include "pmem_log.h"
#include "time_entry.h"
#include "util/autorun_helpers.h"
#include "util/hdr_histogram_wrapper.h"

extern "C" {
#include <raft.h>
}

static constexpr bool kUsePmem = false;

// We have only 3 pmem machines, so client runs on the third server machine
static constexpr bool kColocateClientWithLastServer = kUsePmem;

// We sometimes run a server and client on the same server
static constexpr size_t kAppServerRpcId = 2;  // Rpc ID of all Raft servers
static constexpr size_t kAppClientRpcId = 3;  // Rpc ID of the Raft client

// Key-value configuration

/// Number of keys in the replicated key-value store
static constexpr size_t kAppNumKeys = MB(1);  // 1 million keys ~ ZabFPGA
static_assert(erpc::is_power_of_two(kAppNumKeys), "");

/// Size of values in the replicated key-value store, in bytes
static constexpr size_t kAppValueSize = 32;
static_assert(kAppValueSize % sizeof(size_t) == 0, "");

// Debug/measurement
static constexpr bool kAppTimeEnt = false;
static constexpr bool kAppMeasureCommitLatency = false;  // Leader latency
static constexpr bool kAppVerbose = false;
static constexpr bool kAppEnableRaftConsoleLog = false;  // Non-null console log

// willemt/raft uses a very large 1000 ms election timeout
static constexpr size_t kAppRaftElectionTimeoutMsec = 1000;

// eRPC defines
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;

// We run FLAGS_num_processes processes in the cluster, of which the first
// FLAGS_num_raft_servers are Raft servers, and the remaining are Raft clients.
DEFINE_uint64(num_raft_servers, 0, "Number of Raft servers");

// Return true iff this machine is a Raft server (leader or follower)
bool is_raft_server() { return FLAGS_process_id < FLAGS_num_raft_servers; }

/// The eRPC request types
enum class ReqType : uint8_t {
  kRequestVote = 3,  // Raft requestvote RPC
  kAppendEntries,    // Raft appendentries RPC
  kClientReq         // Client-to-server Rpc
};

/// The value type for the key-value pairs
struct value_t {
  size_t v[kAppValueSize / sizeof(size_t)];
};

/// The client's key-value PUT request = the SMR command replicated in logs
struct client_req_t {
  size_t key;
  value_t value;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[Key (" << std::to_string(key) << "), Value (";
    for (size_t v : value.v) ret << std::to_string(v) << ", ";
    ret << ")]";
    return ret.str();
  }
};

// The client response message
enum class ClientRespType : size_t { kSuccess, kFailRedirect, kFailTryAgain };
struct client_resp_t {
  ClientRespType resp_type;
  int leader_node_id;  // ID of the leader node if resp type is kFailRedirect

  std::string to_string() const {
    switch (resp_type) {
      case ClientRespType::kSuccess: return "success";
      case ClientRespType::kFailRedirect:
        return "failed: redirect to node " + std::to_string(leader_node_id);
      case ClientRespType::kFailTryAgain: return "failed: try again";
    }
    return "Invalid";
  }

  client_resp_t(){};
  client_resp_t(ClientRespType resp_type) : resp_type(resp_type) {}
  client_resp_t(ClientRespType resp_type, int leader_node_id)
      : resp_type(resp_type), leader_node_id(leader_node_id) {}
};

class AppContext;  // Forward declaration

// Peer-peer or client-peer connection
struct connection_t {
  bool disconnected = false;  // True if this session is disconnected
  int session_num = -1;       // eRPC session number
  size_t session_idx = std::numeric_limits<size_t>::max();  // Index in conn_vec
  AppContext *c;
};

// Tag for requests sent to Raft peers (both requestvote and appendentries)
struct raft_req_tag_t {
  erpc::MsgBuffer req_msgbuf;
  erpc::MsgBuffer resp_msgbuf;
  raft_node_t *node;  // The Raft node to which req was sent
};

// Info about client request(s) saved at a leader for the nested Rpc. Each
// Raft server has one of these.
struct leader_saveinfo_t {
  bool in_use = false;          // Leader has an ongoing commit request
  erpc::ReqHandle *req_handle;  // This could be a vector if we do batching
  uint64_t start_tsc;           // Time at which client's request was received
  msg_entry_response_t msg_entry_response;  // Used to check commit status
};

// A log entry serialized into persistent memory
struct pmem_ser_logentry_t {
  raft_entry_t raft_entry;
  client_req_t client_req;

  pmem_ser_logentry_t() {}
  pmem_ser_logentry_t(raft_entry_t r, client_req_t c)
      : raft_entry(r), client_req(c) {}
};

// Context for both servers and clients
class AppContext {
 public:
  // Raft server members
  struct {
    int node_id = -1;  // This server's Raft node ID
    raft_server_t *raft = nullptr;

    // Time since last invocation of raft_periodic() with a non-zero
    // msec_elapsed argument
    size_t raft_periodic_tsc;
    size_t cycles_per_msec;  // rdtsc cycles in one millisecond

    leader_saveinfo_t leader_saveinfo;  // Info for the ongoing commit request
    std::vector<TimeEnt> time_ents;

    // An in-memory pool for application data for Raft log records. In DRAM
    // mode, the Raft log contains pointers to buffers allocated from this pool.
    // In persistent mode, these entries are copied to the DAX file.
    AppMemPool<client_req_t> log_entry_appdata_pool;

    // The presistent memory Raft log, used only if pmem is enabled.
    //
    // In DRAM mode, the user need not provide a log because willemt/raft
    // internally maintains a log. This log contains pointers to volatile bufs
    // allocated from log_entry_appdata_pool.
    PmemLog<pmem_ser_logentry_t> *pmem_log;

    // Request tags used for RPCs exchanged among Raft servers
    AppMemPool<raft_req_tag_t> raft_req_tag_pool;

    /// The key-value store, replicated in the Raft cluster
    std::unordered_map<size_t, value_t> table;

    // Stats
    erpc::Latency commit_latency;            // Amplification factor = 10
    size_t stat_requestvote_enq_fail = 0;    // Failed to send requestvote req
    size_t stat_appendentries_enq_fail = 0;  // Failed to send appendentries req
  } server;

  // SMR client members
  struct {
    size_t leader_idx;  // Client's view of the leader node's index in conn_vec

    size_t num_resps_total = 0;
    size_t num_resps_this_measurement = 0;
    size_t num_console_prints = 0;

    erpc::MsgBuffer req_msgbuf;   // Preallocated req msgbuf
    erpc::MsgBuffer resp_msgbuf;  // Preallocated response msgbuf

    erpc::ChronoTimer chrono_timer;  // For latency measurement
    LatencyUsHdrHistogram lat_us_hdr_histogram;
  } client;

  // Common members
  std::vector<connection_t> conn_vec;
  erpc::Rpc<erpc::CTransport> *rpc = nullptr;
  erpc::FastRand fast_rand;
  size_t num_sm_resps = 0;
};

// Generate a deterministic, random-ish node ID for a process. Process IDs are
// unique at the cluster level. XXX: This can collide!
static int get_raft_node_id_for_process(size_t process_id) {
  std::string uri = erpc::get_uri_for_process(process_id);
  return std::hash<std::string>{}(uri);
}

// eRPC session management handler
void sm_handler(int session_num, erpc::SmEventType sm_event_type,
                erpc::SmErrType sm_err_type, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  c->num_sm_resps++;

  if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the eRPC session number - get the index in conn_vec
  size_t session_idx = c->conn_vec.size();
  for (size_t i = 0; i < c->conn_vec.size(); i++) {
    if (c->conn_vec[i].session_num == session_num) session_idx = i;
  }
  erpc::rt_assert(session_idx < c->conn_vec.size(), "Invalid session number");

  if (sm_event_type == erpc::SmEventType::kDisconnected) {
    c->conn_vec[session_idx].disconnected = true;
  }

  fprintf(stderr,
          "smr: Rpc %u: Session number %d (index %zu) %s. Error = %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          erpc::sm_event_type_str(sm_event_type).c_str(),
          erpc::sm_err_type_str(sm_err_type).c_str(),
          c->rpc->sec_since_creation());
}

// Globals
std::unordered_map<int, std::string> node_id_to_name_map;

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }
