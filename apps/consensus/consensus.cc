/**
 * Copyright (c) 2015, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consensus.h"
#include "appendentries.h"
#include "callbacks.h"
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

void set_raft_callbacks() {
  raft_cbs_t raft_funcs;
  raft_funcs.send_requestvote = __raft_send_requestvote;
  raft_funcs.send_appendentries = __raft_send_appendentries;
  raft_funcs.applylog = __raft_applylog;
  raft_funcs.persist_vote = __raft_persist_vote;
  raft_funcs.persist_term = __raft_persist_term;
  raft_funcs.log_offer = __raft_logentry_offer;
  raft_funcs.log_poll = __raft_logentry_poll;
  raft_funcs.log_pop = __raft_logentry_pop;
  raft_funcs.node_has_sufficient_logs = __raft_node_has_sufficient_logs;
  raft_funcs.log = __raft_log;

  raft_set_callbacks(sv->raft, &raft_funcs, sv);
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

void register_erpc_req_handlers(ERpc::Nexus<ERpc::IBTransport> *nexus) {
  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kRequestVote),
      ERpc::ReqFunc(requestvote_handler, ERpc::ReqFuncType::kForeground));

  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kAppendEntries),
      ERpc::ReqFunc(appendentries_handler, ERpc::ReqFuncType::kForeground));
}

int main(int argc, char **argv) {
  // Work around g++-5's unused variable warning for validators
  _unused(num_raft_servers_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  sv->conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);

  // Initialize Raft at servers. This must be done before running the eRPC event
  // loop, including running it for session management.
  if (is_raft_server()) {
    sv->tsc = ERpc::rdtsc();
    sv->raft = raft_new();
    assert(sv->raft != nullptr);
    set_raft_callbacks();

    sv->node_id = get_raft_node_id_from_hostname(machine_name);
    printf("consensus: Created Raft node with ID = %d.\n", sv->node_id);

    for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
      std::string node_i_hostname = get_hostname_for_machine(i);
      int node_i_id = get_raft_node_id_from_hostname(node_i_hostname);
      node_id_to_name_map[node_i_id] = ERpc::trim_hostname(node_i_hostname);

      if (i == FLAGS_machine_id) {
        // Add self. user_data = nullptr, peer_is_self = 1
        raft_add_node(sv->raft, nullptr, sv->node_id, 1);
      } else {
        raft_add_node(sv->raft, nullptr, node_i_id, 0);  // peer_is_self = 0

        // Link the Raft node to its peer connection struct, which will be
        // filled later when we create the sessions.
        raft_node_t *node = raft_get_node(sv->raft, node_i_id);
        ERpc::rt_assert(node != nullptr, "Could not find added Raft node.");

        peer_connection_t &peer_conn = sv->conn_vec[i];
        peer_conn.node = node;
        raft_node_set_udata(node, static_cast<void *>(&peer_conn));
      }
    }
  }

  // Initialize eRPC
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);
  register_erpc_req_handlers(&nexus);

  // Thread ID = 0
  sv->rpc =
      new ERpc::Rpc<ERpc::IBTransport>(&nexus, static_cast<void *>(sv), 0,
                                       sm_handler, kAppPhyPort, kAppNumaNode);
  sv->rpc->retry_connect_on_invalid_rpc_id = true;

  // Raft client: Create session to each Raft server.
  // Raft server: Create session to each Raft server, excluding self.
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (is_raft_server() && i == FLAGS_machine_id) continue;

    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Creating session to %s, index = %zu.\n",
           hostname.c_str(), i);

    sv->conn_vec[i].session_idx = i;
    sv->conn_vec[i].session_num =
        sv->rpc->create_session(hostname, 0, kAppPhyPort);

    ERpc::rt_assert(sv->conn_vec[i].session_num >= 0,
                    "Failed to create session");
  }

  size_t num_sm_resps_expected =
      is_raft_server() ? FLAGS_num_raft_servers - 1 : FLAGS_num_raft_servers;

  while (sv->num_sm_resps != num_sm_resps_expected) {
    sv->rpc->run_event_loop(200);  // 200 ms
  }

  if (FLAGS_machine_id == 0) raft_become_leader(sv->raft);

  while (ctrl_c_pressed == 0) {
    call_raft_periodic();
    sv->rpc->run_event_loop(0);  // Run once
  }

  delete sv->rpc;  // Free up hugepages
}
