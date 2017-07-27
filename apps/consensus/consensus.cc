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
static int __check_if_ticket_exists(AppContext *c, const unsigned int ticket) {
  assert(c != nullptr && c->check_magic());
  if (c->server.tickets.count(ticket) == 0) return 1;
  return 0;
}

// Generate a ticket that's unique at this node
static unsigned int __generate_ticket(AppContext *c) {
  assert(c != nullptr && c->check_magic());
  unsigned int ticket;

  do {
    ticket = c->fast_rand.next_u32();
  } while (__check_if_ticket_exists(c, ticket));
  return ticket;
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

  AppContext c;
  c.conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers
  for (auto &peer_conn : c.conn_vec) {
    peer_conn.c = &c;
  }

  assert(is_raft_server());  // Handler clients before this point; pass them c

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);

  // Initialize Raft at servers. This must be done before running the eRPC event
  // loop, including running it for session management.
  c.server.raft_periodic_tsc = ERpc::rdtsc();
  c.server.raft = raft_new();
  assert(c.server.raft != nullptr);

  set_raft_callbacks(&c);

  c.server.node_id = get_raft_node_id_from_hostname(machine_name);
  printf("consensus: Created Raft node with ID = %d.\n", c.server.node_id);

  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    std::string node_i_hostname = get_hostname_for_machine(i);
    int node_i_id = get_raft_node_id_from_hostname(node_i_hostname);
    node_id_to_name_map[node_i_id] = ERpc::trim_hostname(node_i_hostname);

    if (i == FLAGS_machine_id) {
      // Add self. user_data = nullptr, peer_is_self = 1
      raft_add_node(c.server.raft, nullptr, c.server.node_id, 1);
    } else {
      // peer_is_self = 0
      raft_add_node(c.server.raft, static_cast<void *>(&c.conn_vec[i]),
                    node_i_id, 0);
    }
  }

  // Initialize eRPC
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);
  register_erpc_req_handlers(&nexus);

  // Thread ID = 0
  c.rpc =
      new ERpc::Rpc<ERpc::IBTransport>(&nexus, static_cast<void *>(&c), 0,
                                       sm_handler, kAppPhyPort, kAppNumaNode);
  c.rpc->retry_connect_on_invalid_rpc_id = true;

  // Raft client: Create session to each Raft server.
  // Raft server: Create session to each Raft server, excluding self.
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (is_raft_server() && i == FLAGS_machine_id) continue;

    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Creating session to %s, index = %zu.\n",
           hostname.c_str(), i);

    c.conn_vec[i].session_idx = i;
    c.conn_vec[i].session_num = c.rpc->create_session(hostname, 0, kAppPhyPort);

    ERpc::rt_assert(c.conn_vec[i].session_num >= 0, "Failed to create session");
  }

  size_t num_sm_resps_expected =
      is_raft_server() ? FLAGS_num_raft_servers - 1 : FLAGS_num_raft_servers;

  while (c.num_sm_resps != num_sm_resps_expected) {
    c.rpc->run_event_loop(200);  // 200 ms
  }

  if (FLAGS_machine_id == 0) raft_become_leader(c.server.raft);

  while (ctrl_c_pressed == 0) {
    call_raft_periodic(&c);
    c.rpc->run_event_loop(0);  // Run once
  }

  delete c.rpc;  // Free up hugepages
}
