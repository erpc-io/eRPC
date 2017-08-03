/**
 * @file client.h
 * @brief The client for the replicated service
 */

#include "consensus.h"

#ifndef CLIENT_H
#define CLIENT_H

enum class ClientRespType : size_t {
  kSuccess,
  kFailLeaderChanged,
  kFailTryAgain
};

// The client request message
struct client_req_t {
  size_t thread_id;
};

// The client response message
struct client_resp_t {
  ClientRespType resp_type;
  union {
    size_t counter;     // The sequence counter
    size_t leader_idx;  // Leader's index in client's conn_vec
  };

  std::string to_string() const {
    switch (resp_type) {
      case ClientRespType::kSuccess:
        return "success, counter = " + std::to_string(counter);
      case ClientRespType::kFailLeaderChanged:
        return "failed (leader changed), leader = " +
               std::to_string(leader_idx);
      case ClientRespType::kFailTryAgain:
        return "failed (try again)";
    }
    return "Invalid";
  }
};

void client_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

void send_req_one(AppContext *c) {
  assert(c != nullptr && c->check_magic());
  c->client.req_latency.stopwatch_start();

  auto *erpc_client_req =
      reinterpret_cast<client_req_t *>(c->client.req_msgbuf.buf);
  erpc_client_req->thread_id = c->client.thread_id;

  if (kAppVerbose) {
    printf("consensus: Client sending request to leader index %zu [%s].\n",
           c->client.leader_idx, ERpc::get_formatted_time().c_str());
  }

  connection_t &conn = c->conn_vec[c->client.leader_idx];
  int ret = c->rpc->enqueue_request(
      conn.session_num, static_cast<uint8_t>(ReqType::kClientReq),
      &c->client.req_msgbuf, &c->client.resp_msgbuf, client_cont, 0);  // 0 tag
  assert(ret == 0);
  _unused(ret);
}

void client_cont(ERpc::RespHandle *resp_handle, void *_context, size_t) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  c->client.req_latency.stopwatch_stop();
  c->client.num_resps++;

  if (c->client.num_resps == 10000) {
    printf("consensus: Client latency = %.2f us.\n",
           c->client.req_latency.get_avg_us());
    c->client.num_resps = 0;
    c->client.req_latency.reset();
  }

  auto *client_resp =
      reinterpret_cast<client_resp_t *>(c->client.resp_msgbuf.buf);

  if (kAppVerbose) {
    printf("consensus: Client received resp %s [%s].\n",
           client_resp->to_string().c_str(),
           ERpc::get_formatted_time().c_str());
  }

  if (unlikely(client_resp->resp_type == ClientRespType::kFailLeaderChanged)) {
    printf("consensus: Client changing leader to index %zu.\n",
           client_resp->leader_idx);
    c->client.leader_idx = client_resp->leader_idx;
  }

  c->rpc->release_response(resp_handle);

  send_req_one(c);
}

void client_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus,
                 AppContext *c) {
  assert(nexus != nullptr && c != nullptr);
  assert(c->conn_vec.size() == FLAGS_num_raft_servers);
  assert(thread_id <= ERpc::kMaxRpcId);

  c->client.thread_id = thread_id;
  c->client.leader_idx = 0;  // Start with leader = 0

  c->rpc = new ERpc::Rpc<ERpc::IBTransport>(
      nexus, static_cast<void *>(c), static_cast<uint8_t>(thread_id),
      sm_handler, kAppPhyPort, kAppNumaNode);
  c->rpc->retry_connect_on_invalid_rpc_id = true;

  // Pre-allocate MsgBuffers
  c->client.req_msgbuf = c->rpc->alloc_msg_buffer(sizeof(client_req_t));
  assert(c->client.req_msgbuf.buf != nullptr);

  c->client.resp_msgbuf = c->rpc->alloc_msg_buffer(sizeof(client_resp_t));
  assert(c->client.resp_msgbuf.buf != nullptr);

  c->client.req_latency = ERpc::TscLatency(c->rpc->get_freq_ghz());

  // Raft client: Create session to each Raft server
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    std::string hostname = get_hostname_for_machine(i);

    printf("consensus: Client %zu creating session to %s, index = %zu.\n",
           thread_id, hostname.c_str(), i);

    c->conn_vec[i].session_idx = i;
    c->conn_vec[i].session_num =
        c->rpc->create_session(hostname, 0, kAppPhyPort);
    assert(c->conn_vec[i].session_num >= 0);
  }

  while (c->num_sm_resps != FLAGS_num_raft_servers) {
    c->rpc->run_event_loop(200);  // 200 ms
    if (ctrl_c_pressed == 1) {
      delete c->rpc;
      exit(0);
    }
  }

  printf("consensus: Client %zu connected to all servers. Sending requests.\n",
         thread_id);

  send_req_one(c);
  while (ctrl_c_pressed == 0) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
  }

  delete c->rpc;
}

#endif
