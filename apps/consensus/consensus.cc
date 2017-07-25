/**
 * Copyright (c) 2015, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consensus.h"

server_t server;
server_t *sv = &server;

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

void send_requestvote_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending request vote message
static int __raft_send_requestvote(raft_server_t *, void *, raft_node_t *node,
                                   msg_requestvote_t *m) {
  assert(node != nullptr);
  auto *conn = static_cast<peer_connection_t *>(raft_node_get_udata(node));
  assert(conn != nullptr);
  assert(conn->node != nullptr);
  assert(conn->session_num >= 0);
  assert(sv->peer_conn_vec[conn->session_idx].session_num == conn->session_num);

  if (kAppVerbose) {
    printf("consensus: __raft_send_requestvote, session index = %zu.\n",
           conn->session_idx);
  }

  if (!sv->rpc->is_connected(conn->session_num)) return 0;

  auto *req_info = new req_info_t();  // XXX: Optimize with pool
  req_info->req_msgbuf =
      sv->rpc->alloc_msg_buffer(sizeof(int) + sizeof(msg_requestvote_t));
  req_info->resp_msgbuf = sv->rpc->alloc_msg_buffer(sizeof(int));

  uint8_t *buf = req_info->req_msgbuf.buf;
  assert(buf != nullptr);

  PeerMessageType *msg_type = reinterpret_cast<PeerMessageType *>(buf);
  *msg_type = PeerMessageType::kRequestVote;

  auto *msg_requestvote =
      reinterpret_cast<msg_requestvote_t *>(buf + sizeof(PeerMessageType));
  *msg_requestvote = *m;

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  int ret = sv->rpc->enqueue_request(
      conn->session_num, kAppReqType, &req_info->req_msgbuf,
      &req_info->resp_msgbuf, send_requestvote_cont, req_tag);
  assert(ret == 0);

  return 0;
}

void send_requestvote_cont(ERpc::RespHandle *resp_handle, void *, size_t tag) {
  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() ==
         sizeof(PeerMessageType) + sizeof(msg_requestvote_response_t));

  uint8_t *buf = req_info->resp_msgbuf.buf;
  assert(buf != nullptr);

  PeerMessageType *msg_type = reinterpret_cast<PeerMessageType *>(buf);
  assert(*msg_type == PeerMessageType::kRequestVoteResp);

  auto *msg_requestvote_resp = reinterpret_cast<msg_requestvote_response_t *>(
      buf + sizeof(PeerMessageType));

  int e = raft_recv_requestvote_response(sv->raft, req_info->node,
                                         msg_requestvote_resp);
  assert(e == 0);  // XXX: Doc says: Shutdown if e != 0

  sv->rpc->free_msg_buffer(req_info->req_msgbuf);
  sv->rpc->free_msg_buffer(req_info->resp_msgbuf);

  sv->rpc->release_response(resp_handle);
}

// Raft callback for saving term field to persistent storage
static int __raft_persist_term(raft_server_t *, void *, const int) {
  // Ignored: We don't do crash recovery.
}

// Raft callback for saving voted_for field to persistent storage.
static int __raft_persist_vote(raft_server_t *, void *, const int) {
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
