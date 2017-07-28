/**
 * @file client.h
 * @brief The client for the replicated service
 */

#include "consensus.h"
#include "util/latency.h"

#ifndef CLIENT_H
#define CLIENT_H

enum class ClientRespType : size_t { kSuccess, kFailLeaderChanged };

static std::string client_resp_type_str(ClientRespType resp_type) {
  switch (resp_type) {
    case ClientRespType::kSuccess:
      return "success";
    case ClientRespType::kFailLeaderChanged:
      return "failed (leader changed)";
  }
  return "Invalid";
}

struct erpc_client_req_t {
  size_t thread_id;
};

struct erpc_client_resp_t {
  ClientRespType resp_type;
  union {
    unsigned int ticket;
    size_t leader_idx;  // Leader's index in client's conn_vec
  };
};

// Request tag, which doubles up as the request descriptor for the request queue
union client_req_tag_t {
  struct {
    uint64_t session_idx : 32;  // Index into context's session_num array
    uint64_t msgbuf_idx : 32;   // Index into context's req_msgbuf array
  };

  size_t _tag;

  client_req_tag_t(uint64_t session_idx, uint64_t msgbuf_idx)
      : session_idx(session_idx), msgbuf_idx(msgbuf_idx) {}
  client_req_tag_t(size_t _tag) : _tag(_tag) {}
  client_req_tag_t() : _tag(0) {}
};
static_assert(sizeof(client_req_tag_t) == sizeof(size_t), "");

void client_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

void send_req_one(AppContext *c) {
  assert(c != nullptr && c->check_magic());

  auto *req_info = new req_info_t();  // XXX: Optimize with pool
  req_info->req_msgbuf = c->rpc->alloc_msg_buffer(sizeof(erpc_client_req_t));
  ERpc::rt_assert(req_info->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  req_info->resp_msgbuf = c->rpc->alloc_msg_buffer(sizeof(erpc_client_resp_t));
  ERpc::rt_assert(req_info->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  auto *erpc_client_req =
      reinterpret_cast<erpc_client_req_t *>(req_info->req_msgbuf.buf);
  erpc_client_req->thread_id = c->client.thread_id;

  if (kAppVerbose) {
    printf("consensus: Client sending request to leader index %zu [%s].\n",
           c->client.leader_idx, ERpc::get_formatted_time().c_str());
  }

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  peer_connection_t &conn = c->conn_vec[c->client.leader_idx];
  int ret = c->rpc->enqueue_request(
      conn.session_num, static_cast<uint8_t>(ReqType::kGetTicket),
      &req_info->req_msgbuf, &req_info->resp_msgbuf, client_cont, req_tag);
  assert(ret == 0);
}

void client_cont(ERpc::RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() == sizeof(erpc_client_resp_t));

  auto *client_resp =
      reinterpret_cast<erpc_client_resp_t *>(req_info->resp_msgbuf.buf);

  if (kAppVerbose) {
    printf("consensus: Client received resp. Type = %s, ticket = %s [%s].\n",
           client_resp_type_str(client_resp->resp_type).c_str(),
           client_resp->resp_type == ClientRespType::kSuccess
               ? std::to_string(client_resp->ticket).c_str()
               : "invalid",
           ERpc::get_formatted_time().c_str());
  }

  if (client_resp->resp_type == ClientRespType::kFailLeaderChanged) {
    printf("consensus: Client changing leader to index %zu.\n",
           client_resp->leader_idx);
    c->client.leader_idx = client_resp->leader_idx;
  }

  c->rpc->free_msg_buffer(req_info->req_msgbuf);
  c->rpc->free_msg_buffer(req_info->resp_msgbuf);
  delete req_info;  // Free allocated memory, XXX: Remove when we use pool

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
