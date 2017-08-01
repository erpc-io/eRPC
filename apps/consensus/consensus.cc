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

// Send a ticket response. This does not free any app-allocated memory.
void send_ticket_response(AppContext *c, ERpc::ReqHandle *req_handle,
                          ticket_resp_t *ticket_resp) {
  if (kAppVerbose) {
    printf("consensus: Sending reply to client: %s [%s].\n",
           ticket_resp->to_string().c_str(),
           ERpc::get_formatted_time().c_str());
  }

  auto *_ticket_resp =
      reinterpret_cast<ticket_resp_t *>(req_handle->pre_resp_msgbuf.buf);
  *_ticket_resp = *ticket_resp;

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                            sizeof(ticket_resp_t));
  req_handle->prealloc_used = true;
  c->rpc->enqueue_response(req_handle);
}

// appendentries request: node ID, msg_appendentries_t, [{size, buf}]
void client_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kTicketReq, ERpc::rdtsc()));
  }

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(ticket_req_t));

  unsigned int ticket = generate_ticket(c);

  if (kAppVerbose) {
    auto *ticket_req = reinterpret_cast<ticket_req_t *>(req_msgbuf->buf);
    printf(
        "consensus: Received client request from client thread %zu. "
        "Assigned ticket = %u [%s].\n",
        ticket_req->thread_id, ticket, ERpc::get_formatted_time().c_str());
  }

  leader_saveinfo_t leader_sav;  // Saved to leader_saveinfo_vec below
  leader_sav.req_handle = req_handle;
  leader_sav.msg_entry_response = new msg_entry_response_t();  // Free on commit

  // Raft will free ticket_buf using a log callback, so we need C-style malloc
  leader_sav.ticket_buf =
      static_cast<unsigned int *>(malloc(sizeof(unsigned int)));
  *leader_sav.ticket_buf = ticket;
  leader_sav.ticket = ticket;  // We need a copy that Raft won't free
  leader_sav.recv_entry_tsc = ERpc::rdtsc();

  c->server.leader_saveinfo_vec.push_back(leader_sav);

  // Receive a log entry. msg_entry can be stack-resident, but not its buf.
  msg_entry_t entry;
  entry.id = c->fast_rand.next_u32();
  entry.data.buf = static_cast<void *>(leader_sav.ticket_buf);
  entry.data.len = sizeof(ticket);

  int e =
      raft_recv_entry(c->server.raft, &entry, leader_sav.msg_entry_response);
  if (e == 0) return;

  // Send a response with the error
  ticket_resp_t err_resp;
  switch (e) {
    case RAFT_ERR_NOT_LEADER:
      // XXX: Redirect to leader
      err_resp.resp_type = TicketRespType::kFailTryAgain;
      break;
    case RAFT_ERR_SHUTDOWN:
      throw std::runtime_error("RAFT_ERR_SHUTDOWN not handled");
    case RAFT_ERR_ONE_VOTING_CHANGE_ONLY:
      err_resp.resp_type = TicketRespType::kFailTryAgain;
      break;
  }
  send_ticket_response(c, req_handle, &err_resp);

  // Clean up
  delete leader_sav.msg_entry_response;
  free(leader_sav.ticket_buf);
  c->server.leader_saveinfo_vec.pop_back();
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
  signal(SIGINT, ctrl_c_handler);

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
  assert(is_raft_server());  // Handle client before this point

  // Initialize Raft at servers. This must be done before running the eRPC event
  // loop, including running it for session management.
  c.server.raft = raft_new();
  assert(c.server.raft != nullptr);
  c.server.time_entry_vec.reserve(1000000);
  c.server.raft_periodic_tsc = ERpc::rdtsc();

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

  size_t loop_tsc = ERpc::rdtsc();
  while (ctrl_c_pressed == 0) {
    if (ERpc::rdtsc() - loop_tsc > 3000000000ull) {
      ERpc::Latency &commit_latency = c.server.commit_latency;
      ERpc::Latency &ae_latency = c.server.appendentries_latency;
      printf(
          "consensus: leader commit latency = {%.2f, %.2f, %2f} us. "
          "appendentries request latency = {%.2f, %.2f, %.2f} us.\n",
          commit_latency.perc(.10) / 10.0, commit_latency.avg() / 10.0,
          commit_latency.perc(.99) / 10.0, ae_latency.perc(.10) / 10.0,
          ae_latency.avg() / 10.0, ae_latency.perc(.99) / 10.0);

      loop_tsc = ERpc::rdtsc();
      commit_latency.reset();
      ae_latency.reset();
    }

    call_raft_periodic(&c);
    c.rpc->run_event_loop(0);  // Run once

    size_t write_index = 0;
    for (auto &leader_sav : c.server.leader_saveinfo_vec) {
      int commit_status = raft_msg_entry_response_committed(
          c.server.raft, leader_sav.msg_entry_response);
      assert(commit_status == 0 || commit_status == 1);

      if (commit_status == 0) {
        // Not committed yet
        c.server.leader_saveinfo_vec[write_index] = leader_sav;
        write_index++;
      } else {
        // Committed: Send a response
        if (kAppCollectTimeEntries) {
          c.server.time_entry_vec.push_back(
              TimeEntry(TimeEntryType::kCommitted, ERpc::rdtsc()));
        }

        ERpc::ReqHandle *req_handle = leader_sav.req_handle;
        double commit_latency = ERpc::to_usec(
            ERpc::rdtsc() - leader_sav.recv_entry_tsc, c.rpc->get_freq_ghz());
        c.server.commit_latency.update(commit_latency * 10);

        ticket_resp_t ticket_resp;
        ticket_resp.resp_type = TicketRespType::kSuccess;
        ticket_resp.ticket = leader_sav.ticket;

        send_ticket_response(&c, req_handle, &ticket_resp);  // Prints message
        delete leader_sav.msg_entry_response;
      }
    }

    c.server.leader_saveinfo_vec.resize(write_index);
  }

  // This is OK even when kAppCollectTimeEntries = false
  printf("consensus: Printing first 1000 of %zu time entries.\n",
         c.server.time_entry_vec.size());
  size_t num_print = std::min(c.server.time_entry_vec.size(), 1000ul);

  if (num_print > 0) {
    size_t base_tsc = c.server.time_entry_vec[0].tsc;
    double freq_ghz = c.rpc->get_freq_ghz();
    for (size_t i = 0; i < num_print; i++) {
      printf("%s\n",
             c.server.time_entry_vec[i].to_string(base_tsc, freq_ghz).c_str());
    }
  }

  printf("consensus: Exiting.\n");
  delete c.rpc;
}
