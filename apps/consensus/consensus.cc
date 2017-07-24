/**
 * Copyright (c) 2015, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

extern "C" {
#include <raft/raft.h>
}

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <tpl.h>
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

enum class PeerMessageType : int {
  // Handshake is a special non-raft message type. We send a handshake so that
  // we can identify ourselves to our peers.
  kHandshake,
  // Successful responses mean we can start the Raft periodic callback.
  kHandshareResp,
  // Tell leader we want to leave the cluster. When instance is ctrl-c'd we have
  // to gracefuly disconnect.
  kLeave,
  // Receiving a leave response means we can shutdown.
  kLeaveResp,
  kRequestVote,
  kRequestVoteResp,
  kAppendEntries,
  kAppendEntriesResp,
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
  int session_num;    // ERpc session number
  tpl_gather_t* gt;   // Gather TPL message
  raft_node_t* node;  // Peer's Raft node index

  // Number of entries currently expected. This counts down as we consume
  // entries.
  int n_expected_entries;

  // Remember most recent append entries msg. We refer to this msg when we
  // finish reading the log entries.
  // Used in tandem with n_expected_entries.
  msg_t ae;
};

struct log_entry_t {
  std::vector<uint8_t> data;
  log_entry_t* next;
};

struct server_t {
  int node_id = -1;  // This server's node ID
  raft_server_t* raft = nullptr;

  // Persistent set of tickets that have been issued.
  std::set<unsigned int> tickets;

  // Persistent state for voted_for and term
  struct {
    int term;
    int voted_for;
  } state;

  // Persistent entries that have been appended to our log
  // For each log entry we store two things next to each other:
  // * TPL serialized raft_entry_t
  // * raft_entry_data_t
  log_entry_t log_head;

  std::vector<peer_connection_t> peer_conn_vec;

  // ERpc members
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

server_t server;
server_t* sv = &server;

// Serialize a peer message using TPL
static size_t __peer_msg_serialize(tpl_node* tn, uv_buf_t* buf, char* data) {
  size_t sz;
  tpl_pack(tn, 0);                 // Pack all elements
  tpl_dump(tn, TPL_GETSIZE, &sz);  // sz gets size of the serialized buffer

  // Serialize into @data
  tpl_dump(tn, TPL_MEM | TPL_PREALLOCD, data, kRaftBuflen);
  tpl_free(tn);
  buf->len = sz;
  buf->base = data;
  return sz;
}

// Check if the ticket has already been issued.
// Return 0 if not unique; otherwise 1.
static int __check_if_ticket_exists(const unsigned int ticket) {
  if (sv->tickets.count(ticket) == 0) return 1;
  return 0;
}

// Generate a ticket that's unique at this node
static unsigned int __generate_ticket() {
  unsigned int ticket;

  do {
    ticket = sv->fast_rand.next_u32();
  } while (__check_if_ticket_exists(ticket));
  return ticket;
}

/** Raft callback for sending request vote message */
static int __raft_send_requestvote(raft_server_t*, void*, raft_node_t* node,
                                   msg_requestvote_t* m) {
  peer_connection_t* conn =
      static_cast<peer_connection_t*>(raft_node_get_udata(node));

  if (!sv->rpc->is_connected(conn->session_num)) return 0;

  uv_buf_t bufs[1];
  char buf[kRaftBuflen];
  msg_t msg = {};
  msg.type = static_cast<int>(PeerMessageType::kRequestVote);
  msg.rv = *m;  // XXX: This makes a copy of the msg_requestvote_t

  tpl_node* tn = tpl_map("S(I$(IIII))", &msg);
  __peer_msg_send(conn->stream, tn, bufs, buf);
  return 0;
}

// Raft callback for saving term field to persistent storage
static int __raft_persist_term(raft_server_t*, void*, const int) {
  // Ignored: We don't do crash recovery.
}

// Raft callback for saving voted_for field to persistent storage.
static int __raft_persist_vote(raft_server_t*, void*, const int) {
  // Ignored: We don't do crash recovery.
}

raft_cbs_t raft_funcs = {
    .send_requestvote = __raft_send_requestvote,
    .send_appendentries = __raft_send_appendentries,
    .applylog = __raft_applylog,
    .persist_vote = __raft_persist_vote,
    .persist_term = __raft_persist_term,
    .log_offer = __raft_logentry_offer,
    .log_poll = __raft_logentry_poll,
    .log_pop = __raft_logentry_pop,
    .node_has_sufficient_logs = __raft_node_has_sufficient_logs,
    .log = __raft_log,
};

// ERpc sessiom management handler
void sm_handler(int session_num, ERpc::SmEventType sm_event_type,
                ERpc::SmErrType sm_err_type, void* _context) {
  assert(_context != nullptr);

  auto* c = static_cast<server_t*>(_context);
  c->num_sm_resps++;

  if (sm_err_type != ERpc::SmErrType::kNoError) {
    throw std::runtime_error("Received SM response with error.");
  }

  if (!(sm_event_type == ERpc::SmEventType::kConnected ||
        sm_event_type == ERpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the ERpc session number - get the index in vector
  size_t session_idx = c->peer_conn_vec.size();
  for (size_t i = 0; i < c->peer_conn_vec.size(); i++) {
    if (c->peer_conn_vec[i].session_num == session_num) {
      session_idx = i;
    }
  }

  if (session_idx == c->peer_conn_vec.size()) {
    throw std::runtime_error("SM callback for invalid session number.");
  }

  fprintf(stderr,
          "large_rpc_tput: Rpc %u: Session number %d (index %zu) %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          sm_event_type == ERpc::SmEventType::kConnected ? "connected"
                                                         : "disconncted",
          c->rpc->sec_since_creation());
}

// Generate a deterministic, random-ish node ID from a machine's hostname
int get_raft_node_id_from_hostname(std::string hostname) {
  uint32_t hash = CityHash32(hostname.c_str(), hostname.length());
  return static_cast<int>(hash);
}

int main() {
  // Initialize eRPC
  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

  sv->rpc =
      new ERpc::Rpc<ERpc::IBTransport>(&nexus, static_cast<void*>(sv),
                                       0,  // Thread ID
                                       sm_handler, kAppPhyPort, kAppNumaNode);
  sv->rpc->retry_connect_on_invalid_rpc_id = true;

  // Raft client: Create session to each Raft server.
  // Raft server: Create session to each Raft server, excluding self.
  sv->peer_conn_vec.resize(FLAGS_num_raft_servers);
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (is_raft_server() && i == FLAGS_machine_id) continue;

    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Creating session to %s, index = %zu.\n",
           hostname.c_str(), i);
    sv->peer_conn_vec[i].session_num =
        sv->rpc->create_session(hostname, 0, kAppPhyPort);

    ERpc::rt_assert(sv->peer_conn_vec[i].session_num >= 0,
                    "Failed to create session");
  }

  size_t num_sm_resps_expected =
      is_raft_server() ? FLAGS_num_raft_servers - 1 : FLAGS_num_raft_servers;

  while (sv->num_sm_resps != num_sm_resps_expected) {
    sv->rpc->run_event_loop(200);  // 200 ms
  }

  if (is_raft_server()) {
    // Initialize Raft
    sv->raft = raft_new();
    raft_set_callbacks(sv->raft, &raft_funcs, sv);

    sv->node_id = get_raft_node_id_from_hostname(machine_name);

    for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
      if (i == FLAGS_machine_id) {
        // Add self. user_data = nullptr, peer_is_self = 1
        raft_add_node(sv->raft, nullptr, sv->node_id, 1);
      } else {
        std::string hostname = get_hostname_for_machine(i);
        int node_id = get_raft_node_id_from_hostname(hostname);
        raft_add_node(sv->raft, nullptr, node_id, 0);  // peer_is_self = 0
      }
    }
  } else {
  }

  if (FLAGS_machine_id == 0) raft_become_leader(sv->raft);

  __start_raft_periodic_timer(sv);

  uv_run(&sv->peer_loop, UV_RUN_DEFAULT);
}
