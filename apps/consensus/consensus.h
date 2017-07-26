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

// We run FLAGS_num_machines in the cluster, of which the first
// FLAGS_num_raft_servers are Raft servers, and the remaining machines are Raft
// clients.
DEFINE_uint64(num_raft_servers, 0,
              "Number of Raft servers (i.e., non-client machines)");
static bool validate_num_raft_servers(const char*, uint64_t num_raft_servers) {
  return num_raft_servers > 0 && num_raft_servers % 2 == 1;
}
DEFINE_validator(num_raft_servers, &validate_num_raft_servers);

// Return true iff this machine is a Raft server
bool is_raft_server() { return FLAGS_machine_id < FLAGS_num_raft_servers; }

enum class HandshakeState { kHandshakeFailure, kHandshakeSuccess };

enum class ReqType : uint8_t {
  kRequestVote = 3,
  kAppendEntries,
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

struct peer_connection_t {
  int session_num = -1;  // ERpc session number
  size_t session_idx = std::numeric_limits<size_t>::max();  // Index in vector
  raft_node_t* node = nullptr;                              // Peer's Raft node

  // Number of entries currently expected. This counts down as we consume
  // entries.
  int n_expected_entries;

  // Remember most recent append entries msg. We refer to this msg when we
  // finish reading the log entries.
  // Used in tandem with n_expected_entries.
  msg_t ae;
};

struct req_info_t {
  raft_node_t* node;  // The Raft node to which this request was sent
  ERpc::MsgBuffer req_msgbuf;
  ERpc::MsgBuffer resp_msgbuf;
};

struct server_t {
  int node_id = -1;  // This server's node ID
  raft_server_t* raft = nullptr;

  // Set of tickets that have been issued.
  std::set<unsigned int> tickets;

  std::vector<peer_connection_t> peer_conn_vec;

  // ERpc-related members
  ERpc::Rpc<ERpc::IBTransport>* rpc;
  ERpc::FastRand fast_rand;
  size_t num_sm_resps = 0;
};

static peer_connection_t* __new_connection(server_t* sv);
static void __connect_to_peer(peer_connection_t* conn);
static void __connection_set_peer(peer_connection_t* conn, char* host);
static void __connect_to_peer_at_host(peer_connection_t* conn, char* host);
static void __start_raft_periodic_timer(server_t* sv);
static int __send_handshake_response(peer_connection_t* conn,
                                     HandshakeState success,
                                     raft_node_t* leader);
static int __send_leave_response(peer_connection_t* conn);

// Generate a deterministic, random-ish node ID from a machine's hostname
int get_raft_node_id_from_hostname(std::string hostname) {
  uint32_t hash = CityHash32(hostname.c_str(), hostname.length());
  return static_cast<int>(hash);
}

// Globals
server_t server;
server_t* sv = &server;

#endif
