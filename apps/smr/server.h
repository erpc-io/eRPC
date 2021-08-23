/**
 * @file server.h
 * @brief Server code
 */

#pragma once

#include "appendentries.h"
#include "log_callbacks.h"
#include "requestvote.h"
#include "smr.h"

// Raft callback for displaying debugging information
void smr_raft_console_log_cb(raft_server_t *, raft_node_t *, void *,
                             const char *buf) {
  if (kAppVerbose) {
    printf("raft: %s [%s].\n", buf, erpc::get_formatted_time().c_str());
  }
}

void set_raft_callbacks(AppContext *c) {
  raft_cbs_t raft_funcs;
  raft_funcs.send_requestvote = smr_raft_send_requestvote_cb;
  raft_funcs.send_appendentries = smr_raft_send_appendentries_cb;
  raft_funcs.send_snapshot = smr_raft_send_snapshot_cb;
  raft_funcs.applylog = smr_raft_applylog_cb;
  raft_funcs.persist_vote = smr_raft_persist_vote_cb;
  raft_funcs.persist_term = smr_raft_persist_term_cb;
  raft_funcs.log_offer = smr_raft_log_offer_cb;
  raft_funcs.log_poll = smr_raft_log_poll_cb;
  raft_funcs.log_pop = smr_raft_log_pop_cb;
  raft_funcs.log_get_node_id = smr_raft_log_get_node_id_cb;
  raft_funcs.node_has_sufficient_logs = smr_raft_node_has_sufficient_logs_cb;
  raft_funcs.notify_membership_event = smr_raft_notify_membership_event_cb;

  // Any non-null console callback will require vsnprintf in willemt/raft
  raft_funcs.log = kAppEnableRaftConsoleLog ? smr_raft_console_log_cb : nullptr;
  raft_set_callbacks(c->server.raft, &raft_funcs, static_cast<void *>(c));
}

void init_raft(AppContext *c) {
  c->server.raft = raft_new();
  raft_set_election_timeout(c->server.raft, kAppRaftElectionTimeoutMsec);

  erpc::rt_assert(c->server.raft != nullptr, "Failed to init raft");

  c->server.node_id = get_raft_node_id_for_process(FLAGS_process_id);
  printf("smr: Created Raft node with ID = %d.\n", c->server.node_id);

  set_raft_callbacks(c);
  if (kUsePmem) {
    c->server.pmem_log =
        new PmemLog<pmem_ser_logentry_t>(erpc::measure_rdtsc_freq());
  }

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

  if (kAppTimeEnt) c->server.time_ents.reserve(1000000);
  c->server.cycles_per_msec = erpc::ms_to_cycles(1, erpc::measure_rdtsc_freq());
  c->server.raft_periodic_tsc = erpc::rdtsc();
}

// Send a response to the client. This does not free any non-eRPC memory.
void send_client_response(AppContext *c, erpc::ReqHandle *req_handle,
                          client_resp_t &client_resp) {
  if (kAppVerbose) {
    printf("smr: Sending reply to client: %s [%s].\n",
           client_resp.to_string().c_str(), erpc::get_formatted_time().c_str());
  }

  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf_;
  auto *_client_resp = reinterpret_cast<client_resp_t *>(resp_msgbuf.buf_);
  *_client_resp = client_resp;

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                            sizeof(client_resp_t));
  c->rpc->enqueue_response(req_handle, &resp_msgbuf);
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
  const auto *client_req = reinterpret_cast<client_req_t *>(req_msgbuf->buf_);

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
  client_req_t *log_entry_appdata = c->server.log_entry_appdata_pool.alloc();
  *log_entry_appdata = *client_req;

  msg_entry_t ent;
  ent.type = RAFT_LOGTYPE_NORMAL;
  ent.id = FLAGS_process_id;
  ent.data.buf = log_entry_appdata;
  ent.data.len = sizeof(client_req_t);

  int e = raft_recv_entry(c->server.raft, &ent, &leader_sav.msg_entry_response);
  erpc::rt_assert(e == 0, "client_req_handle: raft_recv_entry failed");
}

void init_erpc(AppContext *c, erpc::Nexus *nexus) {
  nexus->register_req_func(static_cast<uint8_t>(ReqType::kRequestVote),
                           requestvote_handler);

  nexus->register_req_func(static_cast<uint8_t>(ReqType::kAppendEntries),
                           appendentries_handler);

  nexus->register_req_func(static_cast<uint8_t>(ReqType::kClientReq),
                           client_req_handler);

  c->rpc = new erpc::Rpc<erpc::CTransport>(
      nexus, static_cast<void *>(c), kAppServerRpcId, sm_handler, kAppPhyPort);

  c->rpc->retry_connect_on_invalid_rpc_id_ = true;

  // Create a session to each Raft server, excluding self
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    if (i == FLAGS_process_id) continue;
    std::string uri = erpc::get_uri_for_process(i);
    printf("smr: Creating session to %s, index = %zu.\n", uri.c_str(), i);

    c->conn_vec[i].session_idx = i;
    c->conn_vec[i].session_num = c->rpc->create_session(uri, kAppServerRpcId);
    assert(c->conn_vec[i].session_num >= 0);
  }

  while (c->num_sm_resps != FLAGS_num_raft_servers - 1) {
    c->rpc->run_event_loop(200);  // 200 ms
    if (ctrl_c_pressed == 1) {
      delete c->rpc;
      exit(0);
    }
  }

  printf("smr: All sessions connected\n");
}

inline void call_raft_periodic(AppContext *c) {
  // raft_periodic() uses msec_elapsed for only request and election timeouts.
  // msec_elapsed is in integer milliseconds which does not work for us because
  // we invoke raft_periodic() much work frequently. Instead, we accumulate
  // cycles over calls to raft_periodic().

  size_t cur_tsc = erpc::rdtsc();
  size_t msec_elapsed =
      (cur_tsc - c->server.raft_periodic_tsc) / c->server.cycles_per_msec;

  if (msec_elapsed > 0) {
    c->server.raft_periodic_tsc = cur_tsc;
    raft_periodic(c->server.raft, msec_elapsed);
  } else {
    raft_periodic(c->server.raft, 0);
  }
}

void server_func(erpc::Nexus *nexus, AppContext *c) {
  // The Raft server must be initialized before running the eRPC event loop,
  // including running it for eRPC session management.
  init_raft(c);
  init_erpc(c, nexus);
  c->server.table.reserve(kAppNumKeys * 2);  // Pre-allocate buckets with room

  // The main loop
  size_t loop_tsc = erpc::rdtsc();
  while (ctrl_c_pressed == 0) {
    if (erpc::rdtsc() - loop_tsc > 3000000000ull) {
      erpc::Latency &commit_latency = c->server.commit_latency;

      printf(
          "smr: Leader commit latency (us) = "
          "{%.2f median, %.2f 99%%}. Number of log entries = %ld.\n",
          kAppMeasureCommitLatency ? commit_latency.perc(.50) / 10.0 : -1.0,
          kAppMeasureCommitLatency ? commit_latency.perc(.99) / 10.0 : -1.0,
          raft_get_log_count(c->server.raft));

      loop_tsc = erpc::rdtsc();
      commit_latency.reset();
    }

    call_raft_periodic(c);
    c->rpc->run_event_loop_once();

    leader_saveinfo_t &leader_sav = c->server.leader_saveinfo;
    if (!leader_sav.in_use) {
      // Either we are the leader and don't have an active request, or we
      // are a follower
      continue;
    }

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
  const size_t num_print =
      std::min(c->server.time_ents.size(), static_cast<size_t>(1000));

  if (num_print > 0) {
    size_t base_tsc = c->server.time_ents[0].tsc;
    double freq_ghz = c->rpc->get_freq_ghz();
    for (size_t i = 0; i < num_print; i++) {
      printf("%s\n",
             c->server.time_ents[i].to_string(base_tsc, freq_ghz).c_str());
    }
  }

  printf("smr: Final log size (including uncommitted entries) = %ld\n",
         raft_get_log_count(c->server.raft));
  delete c->rpc;
}
