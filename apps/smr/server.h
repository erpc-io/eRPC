/**
 * @file server.h
 * @brief Server code
 */

#pragma once

#include "appendentries.h"
#include "callbacks.h"
#include "client.h"
#include "requestvote.h"
#include "smr.h"

void init_raft(AppContext *c) {
  c->server.raft = raft_new();
  erpc::rt_assert(c->server.raft != nullptr);

  if (kAppTimeEnt) c->server.time_ents.reserve(1000000);
  c->server.raft_periodic_tsc = erpc::rdtsc();

  set_raft_callbacks(c);

  c->server.node_id = get_raft_node_id_for_process(FLAGS_process_id);
  printf("smr: Created Raft node with ID = %d.\n", c->server.node_id);

  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    int raft_node_id = get_raft_node_id_for_process(i);

    if (i == FLAGS_process_id) {
      // Add self. user_data = nullptr, peer_is_self = 1
      raft_add_node(c->server.raft, nullptr, c->server.node_id, 1);
    } else {
      // Add a non-self node. peer_is_self = 0
      raft_add_node(c->server.raft, static_cast<void *>(&c->conn_vec[i]),
                    raft_node_id, 0);
    }
  }
}

// Send a response to the client. This does not free any non-eRPC memory.
void send_client_response(AppContext *c, erpc::ReqHandle *req_handle,
                          client_resp_t &client_resp) {
  if (kAppVerbose) {
    printf("smr: Sending reply to client: %s [%s].\n",
           client_resp.to_string().c_str(), erpc::get_formatted_time().c_str());
  }

  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf;
  auto *_client_resp = reinterpret_cast<client_resp_t *>(resp_msgbuf.buf);
  *_client_resp = client_resp;

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                            sizeof(client_resp_t));
  req_handle->prealloc_used = true;
  c->rpc->enqueue_response(req_handle);
}

void client_req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // We need leader_sav to start commit latency measurement, but we won't mark
  // it in_use until checking that this node is the leader
  leader_saveinfo_t &leader_sav = c->server.leader_saveinfo;

  if (kAppMeasureCommitLatency) leader_sav.start_tsc = erpc::rdtsc();
  if (kAppTimeEnt) c->server.time_ents.emplace_back(TimeEntType::kClientReq);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(client_req_t));
  const auto *client_req = reinterpret_cast<client_req_t *>(req_msgbuf->buf);

  // Check if it's OK to receive the client's request
  raft_node_t *leader = raft_get_current_leader_node(c->server.raft);
  if (unlikely(leader == nullptr)) {
    printf(
        "smr: Received request %s from client, but leader is unknown. "
        "Asking client to retry later.\n",
        client_req->to_string().c_str());

    client_resp_t err_resp(ClientRespType::kFailTryAgain);
    send_client_response(c, req_handle, err_resp);
    return;
  }

  int leader_node_id = raft_node_get_id(leader);
  if (unlikely(leader_node_id != c->server.node_id)) {
    printf(
        "smr: Received request %s from client, "
        "but leader is %s (not me). Redirecting client.\n",
        client_req->to_string().c_str(),
        node_id_to_name_map.at(leader_node_id).c_str());

    client_resp_t err_resp(ClientRespType::kFailRedirect, leader_node_id);
    send_client_response(c, req_handle, err_resp);
    return;
  }

  // We're the leader
  if (kAppVerbose) {
    printf("smr: Received request %s from client [%s].\n",
           client_req->to_string().c_str(), erpc::get_formatted_time().c_str());
  }

  assert(!leader_sav.in_use);
  leader_sav.in_use = true;
  leader_sav.req_handle = req_handle;

  // Receive a log entry. msg_entry can be stack-resident, but not its buf.
  client_req_t *rsm_cmd_buf = c->server.rsm_cmd_buf_pool.alloc();
  *rsm_cmd_buf = *client_req;

  msg_entry_t ent;
  ent.type = RAFT_LOGTYPE_NORMAL;
  ent.id = FLAGS_process_id;
  ent.data.buf = static_cast<void *>(rsm_cmd_buf);
  ent.data.len = sizeof(client_req_t);

  int e = raft_recv_entry(c->server.raft, &ent, &leader_sav.msg_entry_response);
  erpc::rt_assert(e == 0);
}

void init_erpc(AppContext *c, erpc::Nexus *nexus) {
  nexus->register_req_func(static_cast<uint8_t>(ReqType::kRequestVote),
                           requestvote_handler);

  nexus->register_req_func(static_cast<uint8_t>(ReqType::kAppendEntries),
                           appendentries_handler);

  nexus->register_req_func(static_cast<uint8_t>(ReqType::kClientReq),
                           client_req_handler);

  // Thread ID = 0
  c->rpc = new erpc::Rpc<erpc::CTransport>(nexus, static_cast<void *>(c), 0,
                                           sm_handler, kAppPhyPort);

  c->rpc->retry_connect_on_invalid_rpc_id = true;

  // Create a session to each Raft server, excluding self
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (i == FLAGS_process_id) continue;
    std::string uri = erpc::get_uri_for_process(i);
    printf("smr: Creating session to %s, index = %zu.\n", uri.c_str(), i);

    c->conn_vec[i].session_idx = i;
    c->conn_vec[i].session_num = c->rpc->create_session(uri, 0);
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

void init_mica(AppContext *c) {
  auto config = mica::util::Config::load_file("apps/smr/kv_store.json");
  c->server.table = new FixedTable(config.get("table"), kAppValueSize,
                                   c->rpc->get_huge_alloc());
}

void server_func(size_t, erpc::Nexus *nexus, AppContext *c) {
  // The Raft server must be initialized before running the eRPC event loop,
  // including running it for session management.
  init_raft(c);

  init_erpc(c, nexus);  // Initialize eRPC
  init_mica(c);         // Initialize the key-value store

  // The main loop
  size_t loop_tsc = erpc::rdtsc();
  while (ctrl_c_pressed == 0) {
    if (erpc::rdtsc() - loop_tsc > 3000000000ull) {
      erpc::Latency &commit_latency = c->server.commit_latency;
      printf(
          "smr: Leader commit latency (us) = "
          "{%.2f median, %.2f 99%%}. Log size = %zu.\n",
          kAppMeasureCommitLatency ? commit_latency.perc(.50) / 10.0 : -1.0,
          kAppMeasureCommitLatency ? commit_latency.perc(.99) / 10.0 : -1.0,
          c->server.raft_log.size());

      loop_tsc = erpc::rdtsc();
      commit_latency.reset();
    }

    call_raft_periodic(c);
    c->rpc->run_event_loop_once();

    leader_saveinfo_t &leader_sav = c->server.leader_saveinfo;
    if (!leader_sav.in_use) continue;  // We didn't get a client request

    int commit_status = raft_msg_entry_response_committed(
        c->server.raft, &leader_sav.msg_entry_response);
    assert(commit_status == 0 || commit_status == 1);

    if (commit_status == 1) {
      // Committed: Send a response
      raft_apply_all(c->server.raft);

      leader_sav.in_use = false;
      if (kAppMeasureCommitLatency) {
        size_t commit_cycles = erpc::rdtsc() - leader_sav.start_tsc;
        double commit_usec =
            erpc::to_usec(commit_cycles, c->rpc->get_freq_ghz());
        c->server.commit_latency.update(commit_usec * 10);
      }

      if (kAppTimeEnt)
        c->server.time_ents.emplace_back(TimeEntType::kCommitted);

      // XXX: Is this correct, or should we send response in _apply_log()
      // callback? This doesn't adversely affect failure-free performance.
      client_resp_t client_resp(ClientRespType::kSuccess);

      erpc::ReqHandle *req_handle = leader_sav.req_handle;
      send_client_response(c, req_handle, client_resp);
    }
  }

  // This is OK even when kAppTimeEnt = false
  printf("smr: Printing first 1000 of %zu time entries.\n",
         c->server.time_ents.size());
  size_t num_print = std::min(c->server.time_ents.size(), 1000ul);

  if (num_print > 0) {
    size_t base_tsc = c->server.time_ents[0].tsc;
    double freq_ghz = c->rpc->get_freq_ghz();
    for (size_t i = 0; i < num_print; i++) {
      printf("%s\n",
             c->server.time_ents[i].to_string(base_tsc, freq_ghz).c_str());
    }
  }

  printf("smr: Final log size (including uncommitted entries) = %zu.\n",
         c->server.raft_log.size());
  delete c->rpc;
}
