/**
 * @file appendentries.h
 * @brief Handlers for appendentries RPC
 */

#include "consensus.h"

#ifndef APPENDENTRIES_H
#define APPENDENTRIES_H

// The appendentries request send via eRPC
struct erpc_appendentries_t {
  int node_id;
  msg_appendentries_t ae;

  // Valid iff ae->n_entries > 0. buf field is always invalid. Even keepalive
  // appenentries contain this field (for struct packing reasons).
  msg_entry_t msg_entry;
  // If ae.n_entries > 0, there is a buffer here.
};

// appendentries request: node ID, msg_appendentries_t, [{size, buf}]
void appendentries_handler(ERpc::ReqHandle *req_handle, void *) {
  assert(req_handle != nullptr);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t *buf = req_msgbuf->buf;

  auto *req = reinterpret_cast<erpc_appendentries_t *>(buf);
  bool is_keepalive = req->ae.n_entries == 0;

  if (kAppVerbose) {
    printf(
        "consensus: Received appendentries request (%s) from node %d.\n",
        is_keepalive ? "keepalive" : "non-keepalive", req->node_id);
  }

  assert(req->ae.entries == nullptr);
  req->ae.entries = &req->msg_entry;

  // This does a linear search, which is OK for a small number of Raft servers
  raft_node_t *requester_node = raft_get_node(sv->raft, req->node_id);
  assert(requester_node != nullptr);

  if (is_keepalive) {
    assert(req_msgbuf->get_data_size() == sizeof(erpc_appendentries_t));
  } else {
    assert(req_msgbuf->get_data_size() ==
           sizeof(erpc_appendentries_t) + req->msg_entry.data.len);
    assert(req->msg_entry.data.buf == nullptr);
    req->msg_entry.data.buf = buf + sizeof(erpc_appendentries_t);
  }

  sv->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                             sizeof(msg_appendentries_response_t));
  req_handle->prealloc_used = true;

  int e = raft_recv_appendentries(
    sv->raft, requester_node, &req->ae,
    reinterpret_cast<msg_appendentries_response_t *>(
        req_handle->pre_resp_msgbuf.buf));
  assert(e == 0);

  sv->rpc->enqueue_response(req_handle);
}

void appendentries_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending appendentries message
static int __raft_send_appendentries(raft_server_t *, void *, raft_node_t *node,
                                     msg_appendentries_t *m) {
  assert(node != nullptr);
  auto *conn = static_cast<peer_connection_t *>(raft_node_get_udata(node));
  assert(conn != nullptr);
  assert(conn->session_num >= 0);

  if (kAppVerbose) {
    printf("consensus: Sending appendentries to node %d.\n",
           raft_node_get_id(node));
  }

  if (!sv->rpc->is_connected(conn->session_num)) {
    printf("consensus: Cannot send appendentries (disconnected).\n");
    return 0;
  }

  assert(m->n_entries == 0 || m->n_entries == 1);  // ticketd uses <= 1

  // Compute the request size. Keepalive appendentries requests do not have
  // a buffer.
  size_t req_size = sizeof(size_t) + sizeof(msg_appendentries_t);
  if (m->n_entries > 0) {
    req_size += sizeof(size_t) + static_cast<size_t>(m->entries[0].data.len);
  }

  // Fill in request info (the tag)
  auto *req_info = new req_info_t();  // XXX: Optimize with pool
  req_info->req_msgbuf = sv->rpc->alloc_msg_buffer(req_size);
  ERpc::rt_assert(req_info->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  req_info->resp_msgbuf =
      sv->rpc->alloc_msg_buffer(sizeof(msg_appendentries_response_t));
  ERpc::rt_assert(req_info->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  req_info->node = node;

  // Fill in the request
  auto *erpc_appendentries =
      reinterpret_cast<erpc_appendentries_t *>(req_info->req_msgbuf.buf);

  // Header
  erpc_appendentries->node_id = sv->node_id;
  erpc_appendentries->ae = *m; // ticketd copies all fields one-by-one
  erpc_appendentries->ae.entries = nullptr;

  if (m->n_entries > 0) {
    // msg_entry_t
    erpc_appendentries->msg_entry = m->entries[0];
    erpc_appendentries->msg_entry.data.buf = nullptr;

    // Data
    memcpy(req_info->req_msgbuf.buf + sizeof(erpc_appendentries_t),
           m->entries[0].data.buf, m->entries[0].data.len);
  }

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  int ret = sv->rpc->enqueue_request(
      conn->session_num, static_cast<uint8_t>(ReqType::kAppendEntries),
      &req_info->req_msgbuf, &req_info->resp_msgbuf, appendentries_cont,
      req_tag);
  assert(ret == 0);

  return 0;
}

// Continuation for request vote RPC
void requestvote_cont(ERpc::RespHandle *resp_handle, void *, size_t tag) {
  assert(resp_handle != nullptr);

  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() ==
         sizeof(msg_requestvote_response_t));

  if (kAppVerbose) {
    printf("consensus: Received request vote reply from node %d.\n",
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
