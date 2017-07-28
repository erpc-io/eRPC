/**
 * @file appendentries.h
 * @brief Handlers for appendentries RPC
 */

#include "consensus.h"

#ifndef APPENDENTRIES_H
#define APPENDENTRIES_H

// The appendentries request send via eRPC
struct erpc_appendentries_t {
  int node_id;  // Node ID of the sender
  msg_appendentries_t ae;
  // If ae.n_entries > 0, the msg_entry_t structs are serialized here. Each
  // struct's buf is placed immediately after the struct.
};

// appendentries request: node ID, msg_appendentries_t, [{size, buf}]
void appendentries_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t *buf = req_msgbuf->buf;

  auto *req = reinterpret_cast<erpc_appendentries_t *>(buf);
  assert(req->ae.entries == nullptr);
  req->ae.entries =
      reinterpret_cast<msg_entry_t *>(buf + sizeof(erpc_appendentries_t));

  bool is_keepalive = (req->ae.n_entries == 0);
  if (kAppVerbose) {
    printf("consensus: Received appendentries (%s) req from node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[req->node_id].c_str(),
           ERpc::get_formatted_time().c_str());
  }

  // This does a linear search, which is OK for a small number of Raft servers
  raft_node_t *requester_node = raft_get_node(c->server.raft, req->node_id);
  assert(requester_node != nullptr);

  if (is_keepalive) {
    assert(req_msgbuf->get_data_size() == sizeof(erpc_appendentries_t));
  } else {
    buf += sizeof(erpc_appendentries_t);

    for (size_t i = 0; i < static_cast<size_t>(req->ae.n_entries); i++) {
      auto *msg_entry = reinterpret_cast<msg_entry_t *>(buf);
      assert(msg_entry->data.buf == nullptr);

      msg_entry->data.buf = buf + sizeof(msg_entry_t);
      buf += sizeof(msg_entry_t) + msg_entry->data.len;
    }

    assert(buf == req_msgbuf->buf + req_msgbuf->get_data_size());
  }

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                            sizeof(msg_appendentries_response_t));
  req_handle->prealloc_used = true;

  auto *mar = reinterpret_cast<msg_appendentries_response_t *>(
      req_handle->pre_resp_msgbuf.buf);
  int e =
      raft_recv_appendentries(c->server.raft, requester_node, &req->ae, mar);
  _unused(e);
  assert(e == 0);

  c->rpc->enqueue_response(req_handle);
}

void appendentries_cont(ERpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending appendentries message
static int __raft_send_appendentries(raft_server_t *, void *, raft_node_t *node,
                                     msg_appendentries_t *m) {
  assert(node != nullptr && m != nullptr);

  auto *conn = static_cast<peer_connection_t *>(raft_node_get_udata(node));
  assert(conn != nullptr && conn->session_num >= 0 && conn->c != nullptr);

  AppContext *c = conn->c;
  assert(c->check_magic());

  bool is_keepalive = m->n_entries == 0;
  if (kAppVerbose) {
    printf("consensus: Sending appendentries (%s) to node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[raft_node_get_id(node)].c_str(),
           ERpc::get_formatted_time().c_str());
  }

  if (!c->rpc->is_connected(conn->session_num)) {
    printf("consensus: Cannot send appendentries (disconnected).\n");
    return 0;
  }

  // Compute the request size. Keepalive appendentries requests do not have
  // a buffer, but they have an unused msg_entry_t.
  size_t req_size = sizeof(erpc_appendentries_t);
  for (size_t i = 0; i < static_cast<size_t>(m->n_entries); i++) {
    req_size +=
        sizeof(msg_entry_t) + static_cast<size_t>(m->entries[i].data.len);
  }

  ERpc::rt_assert(req_size <= c->rpc->get_max_msg_size(),
                  "appendentries request too large for eRPC");

  // Fill in request info (the tag)
  auto *req_info = new req_info_t();  // XXX: Optimize with pool
  req_info->req_msgbuf = c->rpc->alloc_msg_buffer(req_size);
  ERpc::rt_assert(req_info->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  req_info->resp_msgbuf =
      c->rpc->alloc_msg_buffer(sizeof(msg_appendentries_response_t));
  ERpc::rt_assert(req_info->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  req_info->node = node;

  // Fill in the header
  auto *erpc_appendentries =
      reinterpret_cast<erpc_appendentries_t *>(req_info->req_msgbuf.buf);

  erpc_appendentries->node_id = c->server.node_id;
  erpc_appendentries->ae = *m;
  erpc_appendentries->ae.entries = nullptr;  // Was local pointer

  // Serialize each entry
  uint8_t *buf = req_info->req_msgbuf.buf + sizeof(erpc_appendentries_t);
  for (size_t i = 0; i < static_cast<size_t>(m->n_entries); i++) {
    auto *msg_entry = reinterpret_cast<msg_entry_t *>(buf);
    *msg_entry = m->entries[i];
    msg_entry->data.buf = nullptr;  // Was local pointer
    buf += sizeof(msg_entry_t);

    assert(m->entries[i].data.len > 0);
    memcpy(buf, m->entries[i].data.buf,
           static_cast<size_t>(m->entries[i].data.len));
    buf += static_cast<size_t>(m->entries[i].data.len);
  }
  assert(buf ==
         req_info->req_msgbuf.buf + req_info->req_msgbuf.get_data_size());

  size_t req_tag = reinterpret_cast<size_t>(req_info);
  int ret = c->rpc->enqueue_request(
      conn->session_num, static_cast<uint8_t>(ReqType::kAppendEntries),
      &req_info->req_msgbuf, &req_info->resp_msgbuf, appendentries_cont,
      req_tag);
  assert(ret == 0 || ret == -EBUSY);  // We checked is_connected above
  if (ret == -EBUSY) c->server.stat_appendentries_enq_fail++;

  // If we failed to send a request, pretend as if we sent it. Raft will retry
  // when it times out, but this must be *extremely* rare since we care about
  // perf of appendentries Rpcs.
  return 0;
}

// Continuation for request vote RPC
void appendentries_cont(ERpc::RespHandle *resp_handle, void *_context,
                        size_t tag) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  auto *req_info = reinterpret_cast<req_info_t *>(tag);
  assert(req_info->resp_msgbuf.get_data_size() ==
         sizeof(msg_appendentries_response_t));

  if (kAppVerbose) {
    printf("consensus: Received appendentries response from node %s [%s].\n",
           node_id_to_name_map[raft_node_get_id(req_info->node)].c_str(),
           ERpc::get_formatted_time().c_str());
  }

  auto *msg_appendentries_response =
      reinterpret_cast<msg_appendentries_response_t *>(
          req_info->resp_msgbuf.buf);

  int e = raft_recv_appendentries_response(c->server.raft, req_info->node,
                                           msg_appendentries_response);
  _unused(e);
  assert(e == 0);

  c->rpc->free_msg_buffer(req_info->req_msgbuf);
  c->rpc->free_msg_buffer(req_info->resp_msgbuf);
  delete req_info;  // Free allocated memory, XXX: Remove when we use pool

  c->rpc->release_response(resp_handle);
}

#endif
