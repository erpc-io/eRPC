/**
 * @file client.h
 * @brief The client for the replicated service
 */

#include "consensus.h"

#ifndef CLIENT_H
#define CLIENT_H

// Tag for ticket request sent by a client
struct ticket_req_tag_t {
  ERpc::MsgBuffer req_msgbuf;
  ERpc::MsgBuffer resp_msgbuf;
};

enum class TicketRespType : size_t {
  kSuccess,
  kFailLeaderChanged,
  kFailTryAgain
};

// The ticket request message
struct ticket_req_t {
  size_t thread_id;
};

// The ticket response message
struct ticket_resp_t {
  TicketRespType resp_type;
  union {
    unsigned int ticket;
    size_t leader_idx;  // Leader's index in client's conn_vec
  };

  std::string to_string() const {
    switch (resp_type) {
      case TicketRespType::kSuccess:
        return "success, ticket = " + std::to_string(ticket);
      case TicketRespType::kFailLeaderChanged:
        return "failed (leader changed), leader = " +
               std::to_string(leader_idx);
      case TicketRespType::kFailTryAgain:
        return "failed (try again)";
    }
    return "Invalid";
  }
};

void client_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

void send_req_one(AppContext *c) {
  assert(c != nullptr && c->check_magic());
  c->client.req_tsc = ERpc::rdtsc();

  auto *ticket_req_tag = new ticket_req_tag_t();  // XXX: Optimize with pool
  ticket_req_tag->req_msgbuf = c->rpc->alloc_msg_buffer(sizeof(ticket_req_t));
  ERpc::rt_assert(ticket_req_tag->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  ticket_req_tag->resp_msgbuf = c->rpc->alloc_msg_buffer(sizeof(ticket_resp_t));
  ERpc::rt_assert(ticket_req_tag->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  auto *erpc_client_req =
      reinterpret_cast<ticket_req_t *>(ticket_req_tag->req_msgbuf.buf);
  erpc_client_req->thread_id = c->client.thread_id;

  if (kAppVerbose) {
    printf("consensus: Client sending request to leader index %zu [%s].\n",
           c->client.leader_idx, ERpc::get_formatted_time().c_str());
  }

  connection_t &conn = c->conn_vec[c->client.leader_idx];
  int ret = c->rpc->enqueue_request(
      conn.session_num, static_cast<uint8_t>(ReqType::kGetTicket),
      &ticket_req_tag->req_msgbuf, &ticket_req_tag->resp_msgbuf, client_cont,
      reinterpret_cast<size_t>(ticket_req_tag));
  assert(ret == 0);
  _unused(ret);
}

void client_cont(ERpc::RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  // Measure latency
  double us =
      ERpc::to_usec(ERpc::rdtsc() - c->client.req_tsc, c->rpc->get_freq_ghz());
  c->client.req_latency.update(static_cast<size_t>(us));
  c->client.num_resps++;

  if (c->client.num_resps == 1000) {
    printf("consensus: Client latency = %.2f us avg, %zu 99 perc.\n",
           c->client.req_latency.avg(), c->client.req_latency.perc(.99));
    c->client.num_resps = 0;
    c->client.req_latency.reset();
  }

  auto *ticket_req_tag = reinterpret_cast<ticket_req_tag_t *>(tag);
  assert(ticket_req_tag->resp_msgbuf.get_data_size() == sizeof(ticket_resp_t));

  auto *client_resp =
      reinterpret_cast<ticket_resp_t *>(ticket_req_tag->resp_msgbuf.buf);

  if (kAppVerbose) {
    printf("consensus: Client received resp %s [%s].\n",
           client_resp->to_string().c_str(),
           ERpc::get_formatted_time().c_str());
  }

  if (client_resp->resp_type == TicketRespType::kFailLeaderChanged) {
    printf("consensus: Client changing leader to index %zu.\n",
           client_resp->leader_idx);
    c->client.leader_idx = client_resp->leader_idx;
  }

  c->rpc->free_msg_buffer(ticket_req_tag->req_msgbuf);
  c->rpc->free_msg_buffer(ticket_req_tag->resp_msgbuf);
  delete ticket_req_tag;  // Free allocated memory, XXX: Remove when we use pool

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
