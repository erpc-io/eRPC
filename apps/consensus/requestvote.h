/**
 * @file requestvote.h
 * @brief Handlers for requestvote RPC
 */

#include "consensus.h"

#ifndef REQUESTVOTE_H
#define REQUESTVOTE_H

// The requestvote request send via eRPC
struct erpc_requestvote_t {
  int node_id;
  msg_requestvote_t rv;
};

void requestvote_handler(ERpc::ReqHandle *req_handle, void *) {
  assert(req_handle != nullptr);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(erpc_requestvote_t));

  auto *req = reinterpret_cast<erpc_requestvote_t *>(req_msgbuf->buf);

  if (kAppVerbose) {
    printf("consensus: Received requestvote request from node %d.\n",
           req->node_id);
  }

  // This does a linear search, which is OK for a small number of Raft servers
  raft_node_t *requester_node = raft_get_node(sv->raft, req->node_id);
  assert(requester_node != nullptr);

  sv->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                             sizeof(msg_requestvote_response_t));
  req_handle->prealloc_used = true;

  int e = raft_recv_requestvote(
      sv->raft, requester_node, &req->rv,
      reinterpret_cast<msg_requestvote_response_t *>(
          req_handle->pre_resp_msgbuf.buf));
  assert(e == 0);

  sv->rpc->enqueue_response(req_handle);
}

void requestvote_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending requestvote request
static int __raft_send_requestvote(raft_server_t *, void *, raft_node_t *node,
                                   msg_requestvote_t *m) {
  assert(node != nullptr);
  auto *conn = static_cast<peer_connection_t *>(raft_node_get_udata(node));
  assert(conn != nullptr);
  assert(conn->session_num >= 0);

  if (!sv->rpc->is_connected(conn->session_num)) {
    printf("consensus: Cannot send requestvote request (disconnected).\n");
    return 0;
  }

  if (kAppVerbose) {
    printf("consensus: Sending requestvote request to node %d.\n",
           raft_node_get_id(node));
  }

  auto *req_info = new req_info_t();  // XXX: Optimize with pool
  req_info->req_msgbuf = sv->rpc->alloc_msg_buffer(sizeof(erpc_requestvote_t));
  ERpc::rt_assert(req_info->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  req_info->resp_msgbuf =
      sv->rpc->alloc_msg_buffer(sizeof(msg_requestvote_response_t));
  ERpc::rt_assert(req_info->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  req_info->node = node;

  auto *erpc_requestvote =
      reinterpret_cast<erpc_requestvote_t *>(req_info->req_msgbuf.buf);
  erpc_requestvote->node_id = sv->node_id;
  erpc_requestvote->rv = *m;

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  int ret = sv->rpc->enqueue_request(
      conn->session_num, static_cast<uint8_t>(ReqType::kRequestVote),
      &req_info->req_msgbuf, &req_info->resp_msgbuf, requestvote_cont, req_tag);
  assert(ret == 0);

  return 0;
}

void requestvote_cont(ERpc::RespHandle *resp_handle, void *, size_t tag) {
  assert(resp_handle != nullptr);

  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() ==
         sizeof(msg_requestvote_response_t));

  if (kAppVerbose) {
    printf("consensus: Received requestvote reply from node %d.\n",
           raft_node_get_id(req_info->node));
  }

  uint8_t *buf = req_info->resp_msgbuf.buf;
  assert(buf != nullptr);

  auto *msg_requestvote_resp =
      reinterpret_cast<msg_requestvote_response_t *>(buf);

  int e = raft_recv_requestvote_response(sv->raft, req_info->node,
                                         msg_requestvote_resp);
  assert(e == 0);  // XXX: Doc says: Shutdown if e != 0

  sv->rpc->free_msg_buffer(req_info->req_msgbuf);
  sv->rpc->free_msg_buffer(req_info->resp_msgbuf);
  delete req_info;  // Free allocated memory, XXX: Remove when we use pool

  sv->rpc->release_response(resp_handle);
}

#endif
