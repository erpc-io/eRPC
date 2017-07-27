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

static constexpr bool kAppVerbose = true;

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kRaftBuflen = 512;
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

enum class HandshakeState { kHandshakeFailure, kHandshakeSuccess };

/// The eRPC request types
enum class ReqType : uint8_t {
  kRequestVote = 3,  // Raft requestvote RPC
  kAppendEntries,    // Raft appendentries RPC
  kGetTicket         // Client-to-server Rpc
};

// Peer protocol handshake, sent after connecting so that peer can identify us
struct msg_handshake_t {
  int node_id;
};

struct msg_handshake_response_t {
  int success;
  // My Raft node ID. Sometimes we don't know who we did the handshake with.
  int node_id;
  char leader_host[kIPStrLen];
};

// Add/remove Raft peer
struct entry_cfg_change_t {
  int node_id;
  char host[kIPStrLen];
};

struct msg_t {
  int type;
  union {
    msg_handshake_t hs;
    msg_handshake_response_t hsr;
    msg_requestvote_t rv;
    msg_requestvote_response_t rvr;
    msg_appendentries_t ae;
    msg_appendentries_response_t aer;
  };
  int padding[100];  // XXX: Why do we need this?
};

class AppContext;  // Forward declaration

struct peer_connection_t {
  int session_num = -1;  // ERpc session number
  size_t session_idx = std::numeric_limits<size_t>::max();  // Index in vector

  AppContext *c;  // Back link to AppContext
};

struct req_info_t {
  raft_node_t *node;  // The Raft node to which this request was sent
  ERpc::MsgBuffer req_msgbuf;
  ERpc::MsgBuffer resp_msgbuf;
};

// Context for both servers and clients
class AppContext {
 public:
  static constexpr size_t kAppContextMagic = 0x3185;
  // Server-only members
  struct {
    int node_id = -1;  // This server's Raft node ID
    raft_server_t *raft = nullptr;
    size_t raft_periodic_tsc;  // rdtsc timestamp

    // Set of tickets that have been issued.
    std::set<unsigned int> tickets;

    // Stats
    size_t stat_requestvote_enq_fail = 0;    // Failed to send requestvote req
    size_t stat_appendentries_enq_fail = 0;  // Failed to send appendentries req
  } server;

  std::vector<peer_connection_t> conn_vec;

  // ERpc-related members
  ERpc::Rpc<ERpc::IBTransport> *rpc;
  ERpc::FastRand fast_rand;
  size_t num_sm_resps = 0;

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
  size_t cur_tsc = ERpc::rdtsc();

  double msec_since_last_nonzero = ERpc::to_msec(
      cur_tsc - c->server.raft_periodic_tsc, c->rpc->get_freq_ghz());

  if (msec_since_last_nonzero < 1.0) {
    raft_periodic(c->server.raft, 0);
  } else {
    c->server.raft_periodic_tsc = cur_tsc;
    raft_periodic(c->server.raft, 1);
  }
}

#endif
