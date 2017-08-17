#include "consensus.h"
#include "appendentries.h"
#include "callbacks.h"
#include "client.h"
#include "requestvote.h"

// Send a response to the client. This does not free any non-ERpc memory.
void send_client_response(AppContext *c, ERpc::ReqHandle *req_handle,
                          client_resp_t *client_resp) {
  if (kAppVerbose) {
    printf("consensus: Sending reply to client: %s [%s].\n",
           client_resp->to_string().c_str(),
           ERpc::get_formatted_time().c_str());
  }

  auto *_client_resp =
      reinterpret_cast<client_resp_t *>(req_handle->pre_resp_msgbuf.buf);
  *_client_resp = *client_resp;

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                            sizeof(client_resp_t));
  req_handle->prealloc_used = true;
  c->rpc->enqueue_response(req_handle);
}

void client_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  if (kAppMeasureCommitLatency) c->server.commit_latency.stopwatch_start();
  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kClientReq, ERpc::rdtsc()));
  }

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(client_req_t));
  const auto *client_req = reinterpret_cast<client_req_t *>(req_msgbuf->buf);

  // Check if it's OK to receive the client's request
  raft_node_t *leader = raft_get_current_leader_node(c->server.raft);
  if (unlikely(leader == nullptr)) {
    printf(
        "consensus: Received request from client %zu, but leader unknown. "
        "Asking client to retry later.\n",
        client_req->client_id);

    client_resp_t err_resp;
    err_resp.resp_type = ClientRespType::kFailTryAgain;
    send_client_response(c, req_handle, &err_resp);
    return;
  }

  int leader_node_id = raft_node_get_id(leader);
  if (unlikely(leader_node_id != c->server.node_id)) {
    printf(
        "consensus: Received request from client %zu, "
        "but leader is %s (not me). Redirecting client.\n",
        client_req->client_id, node_id_to_name_map.at(leader_node_id).c_str());
    client_resp_t err_resp;
    err_resp.resp_type = ClientRespType::kFailRedirect;
    err_resp.leader_node_id = leader_node_id;
    send_client_response(c, req_handle, &err_resp);
    return;
  }

  // We're the leader
  if (kAppVerbose) {
    printf("consensus: Received client request from client %zu [%s].\n",
           client_req->client_id, ERpc::get_formatted_time().c_str());
  }

  leader_saveinfo_t &leader_sav = c->server.leader_saveinfo;
  assert(!leader_sav.in_use);
  leader_sav.in_use = true;
  leader_sav.req_handle = req_handle;

  size_t *rsm_cmd_buf = static_cast<size_t *>(c->rsm_cmd_buf_pool_alloc());
  *rsm_cmd_buf = client_req->client_id;

  // Receive a log entry. msg_entry can be stack-resident, but not its buf.
  msg_entry_t entry;
  entry.type = RAFT_LOGTYPE_NORMAL;
  entry.id = FLAGS_machine_id;
  entry.data.buf = static_cast<void *>(rsm_cmd_buf);
  entry.data.len = sizeof(size_t);

  int e =
      raft_recv_entry(c->server.raft, &entry, &leader_sav.msg_entry_response);
  ERpc::rt_assert(e == 0, "raft_recv_entry() failed");
}

void init_raft(AppContext *c) {
  c->server.raft = raft_new();
  assert(c->server.raft != nullptr);

  if (kAppCollectTimeEntries) c->server.time_entry_vec.reserve(1000000);
  c->server.raft_periodic_tsc = ERpc::rdtsc();

  set_raft_callbacks(c);

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  c->server.node_id = get_raft_node_id_from_hostname(machine_name);
  printf("consensus: Created Raft node with ID = %d.\n", c->server.node_id);

  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    std::string node_i_hostname = get_hostname_for_machine(i);
    int node_i_id = get_raft_node_id_from_hostname(node_i_hostname);
    node_id_to_name_map[node_i_id] = ERpc::trim_hostname(node_i_hostname);

    if (i == FLAGS_machine_id) {
      // Add self. user_data = nullptr, peer_is_self = 1
      raft_add_node(c->server.raft, nullptr, c->server.node_id, 1);
    } else {
      // peer_is_self = 0
      raft_add_node(c->server.raft, static_cast<void *>(&c->conn_vec[i]),
                    node_i_id, 0);
    }
  }
}

void init_erpc(AppContext *c, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kRequestVote),
      ERpc::ReqFunc(requestvote_handler, ERpc::ReqFuncType::kForeground));

  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kAppendEntries),
      ERpc::ReqFunc(appendentries_handler, ERpc::ReqFuncType::kForeground));

  nexus->register_req_func(
      static_cast<uint8_t>(ReqType::kClientReq),
      ERpc::ReqFunc(client_req_handler, ERpc::ReqFuncType::kForeground));

  // Thread ID = 0
  c->rpc = new ERpc::Rpc<ERpc::IBTransport>(
      nexus, static_cast<void *>(c), 0, sm_handler, kAppPhyPort, kAppNumaNode);

  c->rpc->retry_connect_on_invalid_rpc_id = true;
  c->server.commit_latency = ERpc::TscLatency(c->rpc->get_freq_ghz());

  // Create a session to each Raft server, excluding self
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (i == FLAGS_machine_id) continue;

    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Creating session to %s, index = %zu.\n",
           hostname.c_str(), i);

    c->conn_vec[i].session_idx = i;
    c->conn_vec[i].session_num =
        c->rpc->create_session(hostname, 0, kAppPhyPort);
    assert(c->conn_vec[i].session_num >= 0);
  }

  while (c->num_sm_resps != FLAGS_num_raft_servers - 1) {
    c->rpc->run_event_loop(200);  // 200 ms
    if (ctrl_c_pressed == 1) {
      delete c->rpc;
      exit(0);
    }
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);

  // Work around g++-5's unused variable warning for validators
  _unused(num_raft_servers_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  AppContext c;
  c.conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers
  for (auto &peer_conn : c.conn_vec) peer_conn.c = &c;

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

  if (!is_raft_server()) {
    // Run client
    auto client_thread = std::thread(client_func, 0, &nexus, &c);
    client_thread.join();
    return 0;
  }
  assert(is_raft_server());  // Handle client before this point

  // The Raft server must be initialized before running the eRPC event loop,
  // including running it for session management.
  init_raft(&c);

  // Initialize eRPC
  init_erpc(&c, &nexus);
  c.rsm_cmd_buf_pool_extend();

  if (FLAGS_machine_id == 0) raft_become_leader(c.server.raft);

  // The main loop
  size_t loop_tsc = ERpc::rdtsc();
  while (ctrl_c_pressed == 0) {
    if (ERpc::rdtsc() - loop_tsc > 3000000000ull) {
      ERpc::TscLatency &commit_latency = c.server.commit_latency;
      printf("consensus: Leader commit latency = %.2f us. Log size = %zu.\n",
             commit_latency.get_avg_us(), c.server.raft_log.size());

      loop_tsc = ERpc::rdtsc();
      commit_latency.reset();
    }

    call_raft_periodic(&c);
    c.rpc->run_event_loop_once();

    leader_saveinfo_t &leader_sav = c.server.leader_saveinfo;
    if (!leader_sav.in_use) continue;  // Avoid passing garbage to commit check

    int commit_status = raft_msg_entry_response_committed(
        c.server.raft, &leader_sav.msg_entry_response);
    assert(commit_status == 0 || commit_status == 1);

    if (commit_status == 1) {
      // Committed: Send a response
      leader_sav.in_use = false;
      if (kAppMeasureCommitLatency) c.server.commit_latency.stopwatch_stop();

      if (kAppCollectTimeEntries) {
        c.server.time_entry_vec.push_back(
            TimeEntry(TimeEntryType::kCommitted, ERpc::rdtsc()));
      }

      client_resp_t client_resp;
      client_resp.resp_type = ClientRespType::kSuccess;

      // XXX: Is this correct, or should we send response in _apply_log()
      // callback? This doesn't adversely affect failure-free performance.
      client_resp.counter = c.server.counter;

      ERpc::ReqHandle *req_handle = leader_sav.req_handle;
      send_client_response(&c, req_handle, &client_resp);  // Prints message
    }
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

  printf(
      "consensus: Final log size (including uncommitted entries) = %zu. "
      "Final counter = %zu. Exiting.\n",
      c.server.raft_log.size(), c.server.counter);
  delete c.rpc;
}
