/**
 * @file request_vote.h
 * @brief Handlers for request_vote RPC
 */

#include "consensus.h"

#ifndef REQUEST_VOTE_H
#define REQUEST_VOTE_H

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
  req_info->req_msgbuf = sv->rpc->alloc_msg_buffer(sizeof(msg_requestvote_t));
  ERpc::rt_assert(req_info->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  req_info->resp_msgbuf =
      sv->rpc->alloc_msg_buffer(sizeof(msg_requestvote_response_t));
  ERpc::rt_assert(req_info->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  auto *msg_requestvote =
      reinterpret_cast<msg_requestvote_t *>(req_info->req_msgbuf.buf);
  *msg_requestvote = *m;

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  int ret = sv->rpc->enqueue_request(
      conn->session_num, static_cast<uint8_t>(ReqType::kRequestVote),
      &req_info->req_msgbuf, &req_info->resp_msgbuf, send_requestvote_cont,
      req_tag);
  assert(ret == 0);

  return 0;
}

void send_requestvote_cont(ERpc::RespHandle *resp_handle, void *, size_t tag) {
  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() == sizeof(size_t));

  uint8_t *buf = req_info->resp_msgbuf.buf;
  assert(buf != nullptr);

  auto *msg_requestvote_resp =
      reinterpret_cast<msg_requestvote_response_t *>(buf);

  int e = raft_recv_requestvote_response(sv->raft, req_info->node,
                                         msg_requestvote_resp);
  assert(e == 0);  // XXX: Doc says: Shutdown if e != 0

  sv->rpc->free_msg_buffer(req_info->req_msgbuf);
  sv->rpc->free_msg_buffer(req_info->resp_msgbuf);

  sv->rpc->release_response(resp_handle);
}

#endif
