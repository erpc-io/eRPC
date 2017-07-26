/**
 * Copyright (c) 2015, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consensus.h"
#include "appendentries.h"
#include "requestvote.h"

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

// Raft callback for applying an entry to the finite state machine
static int __raft_applylog(raft_server_t *, void *, raft_entry_t *ety) {
  assert(!raft_entry_is_cfg_change(ety));
  assert(ety->data.len == sizeof(int));

  unsigned int *ticket = static_cast<unsigned int *>(ety->data.buf);
  if (kAppVerbose) {
    printf("consensus: Adding ticket %d.\n", *ticket);
  }

  sv->tickets.insert(*ticket);
  return 0;
}

// Persistence callbacks are all ignored - we don't maintain a persistent log.

// Raft callback for saving voted_for field to persistent storage.
static int __raft_persist_vote(raft_server_t *, void *, const int) {
  return 0;  // Ignored
}

// Raft callback for saving term field to persistent storage
static int __raft_persist_term(raft_server_t *, void *, const int) {
  return 0;  // Ignored
}

// Raft callback for appending an item to the log
static int __raft_logentry_offer(raft_server_t *, void *, raft_entry_t *ety,
                                 int) {
  assert(!raft_entry_is_cfg_change(ety));
  return 0;  // Ignored
}

// Raft callback for deleting the most recent entry from the log. This happens
// when an invalid leader finds a valid leader and has to delete superseded
// log entries.
static int __raft_logentry_pop(raft_server_t *, void *, raft_entry_t *, int) {
  return 0;  // Ignored
}

// Raft callback for removing the first entry from the log. This is provided to
// support log compaction in the future.
static int __raft_logentry_poll(raft_server_t *, void *, raft_entry_t *, int) {
  return 0;  // Ignored
}

// Non-voting node now has enough logs to be able to vote. Append a finalization
// cfg log entry.
static void __raft_node_has_sufficient_logs(raft_server_t *, void *,
                                            raft_node_t *) {
  assert(false);  // Ignored
}

// Raft callback for displaying debugging information
void __raft_log(raft_server_t *, raft_node_t *, void *, const char *buf) {
  printf("raft: %s\n", buf);
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
                ERpc::SmErrType sm_err_type, void *_context) {
  assert(_context != nullptr);

  auto *c = static_cast<server_t *>(_context);
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

int main() {
  // Initialize eRPC
  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

  sv->rpc =
      new ERpc::Rpc<ERpc::IBTransport>(&nexus, static_cast<void *>(sv),
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

    sv->peer_conn_vec[i].session_idx = i;
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
        std::string peer_hostname = get_hostname_for_machine(i);
        int peer_node_id = get_raft_node_id_from_hostname(peer_hostname);
        raft_add_node(sv->raft, nullptr, peer_node_id, 0);  // peer_is_self = 0

        // Link the node to the peer connection
        raft_node_t *node = raft_get_node(sv->raft, peer_node_id);
        ERpc::rt_assert(node != nullptr, "Could not find added Raft node.");

        peer_connection_t &peer_conn = sv->peer_conn_vec[i];
        peer_conn.node = node;
        raft_node_set_udata(node, static_cast<void *>(&peer_conn));
      }
    }
  } else {
    // Raft client
  }

  if (FLAGS_machine_id == 0) raft_become_leader(sv->raft);

  __start_raft_periodic_timer(sv);

  uv_run(&sv->peer_loop, UV_RUN_DEFAULT);
}
