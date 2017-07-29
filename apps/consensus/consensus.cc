/**
 * Copyright (c) 2015, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consensus.h"
#include "appendentries.h"
#include "callbacks.h"
#include "client.h"
#include "requestvote.h"

// Generate a ticket that's unique at this node
static unsigned int generate_ticket(AppContext *c) {
  assert(c != nullptr && c->check_magic());
  unsigned int ticket = c->fast_rand.next_u32();

  while (c->server.tickets.count(ticket) > 0) {
    ticket = c->fast_rand.next_u32();
  }
  return ticket;
}

// appendentries request: node ID, msg_appendentries_t, [{size, buf}]
void client_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(erpc_client_req_t));

  unsigned int ticket = generate_ticket(c);

  if (kAppVerbose) {
    auto *req = reinterpret_cast<erpc_client_req_t *>(req_msgbuf->buf);
    printf(
        "consensus: Received client request from client thread %zu. "
        "Assigned ticket = %u [%s].\n",
        req->thread_id, ticket, ERpc::get_formatted_time().c_str());
  }

  client_req_info_t client_req_info;
  client_req_info.req_handle = req_handle;

  // We'll free msg_entry_response when the entry commits
  client_req_info.msg_entry_response = new msg_entry_response_t();

  // Raft will free ticket_buf using a log callback
  client_req_info.ticket_buf =
      static_cast<unsigned int *>(malloc(sizeof(unsigned int)));
  *client_req_info.ticket_buf = ticket;

  // We need a copy of the allocated ticket that won't get freed by Raft
  client_req_info.ticket = ticket;

  c->server.client_req_vec.push_back(client_req_info);

  // Receive a log entry into Raft - the msg_entry_t doesn't need to be
  // dynamically allocated.
  msg_entry_t entry;
  entry.id = c->fast_rand.next_u32();
  entry.data.buf = static_cast<void *>(client_req_info.ticket_buf);
  entry.data.len = sizeof(ticket);

  // entry can be static, but its buf must survive after this function returns
  int e = raft_recv_entry(c->server.raft, &entry,
                          client_req_info.msg_entry_response);
  assert(e == 0);
  _unused(e);
}

void register_erpc_req_handlers(ERpc::Nexus<ERpc::IBTransport> *nexus) {
  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kRequestVote),
      ERpc::ReqFunc(requestvote_handler, ERpc::ReqFuncType::kForeground));

  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kAppendEntries),
      ERpc::ReqFunc(appendentries_handler, ERpc::ReqFuncType::kForeground));

  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kGetTicket),
      ERpc::ReqFunc(client_req_handler, ERpc::ReqFuncType::kForeground));
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

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

  if (!is_raft_server()) {
    // Run client
    auto client_thread = std::thread(client_func, 0, &nexus, &c);
    client_thread.join();
    return 0;
  }

  assert(is_raft_server());  // Handler clients before this point; pass them c

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
  register_erpc_req_handlers(&nexus);

  // Thread ID = 0
  c.rpc =
      new ERpc::Rpc<ERpc::IBTransport>(&nexus, static_cast<void *>(&c), 0,
                                       sm_handler, kAppPhyPort, kAppNumaNode);
  c.rpc->retry_connect_on_invalid_rpc_id = true;

  // Raft server: Create session to each Raft server, excluding self.
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (i == FLAGS_machine_id) continue;

    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Creating session to %s, index = %zu.\n",
           hostname.c_str(), i);

    c.conn_vec[i].session_idx = i;
    c.conn_vec[i].session_num = c.rpc->create_session(hostname, 0, kAppPhyPort);
    assert(c.conn_vec[i].session_num >= 0);
  }

  while (c.num_sm_resps != FLAGS_num_raft_servers - 1) {
    c.rpc->run_event_loop(200);  // 200 ms
  }

  if (FLAGS_machine_id == 0) raft_become_leader(c.server.raft);

  while (ctrl_c_pressed == 0) {
    call_raft_periodic(&c);
    c.rpc->run_event_loop(0);  // Run once

    size_t write_index = 0;
    for (auto &client_req_info : c.server.client_req_vec) {
      int commit_status = raft_msg_entry_response_committed(
          c.server.raft, client_req_info.msg_entry_response);
      assert(commit_status == 0 || commit_status == 1);

      if (commit_status == 0) {
        // Not committed yet
        c.server.client_req_vec[write_index] = client_req_info;
        write_index++;
      } else {
        // Committed: Send a response
        ERpc::ReqHandle *req_handle = client_req_info.req_handle;
        c.rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                 sizeof(erpc_client_resp_t));
        req_handle->prealloc_used = true;

        auto *client_resp = reinterpret_cast<erpc_client_resp_t *>(
            req_handle->pre_resp_msgbuf.buf);
        client_resp->resp_type = ClientRespType::kSuccess;
        client_resp->ticket = client_req_info.ticket;

        if (kAppVerbose) {
          printf("consensus: Replying to client with ticket = %u.\n",
                 client_resp->ticket);
        }

        c.rpc->enqueue_response(req_handle);
        delete client_req_info.msg_entry_response;
      }
    }

    c.server.client_req_vec.resize(write_index);
  }

  delete c.rpc;
}
