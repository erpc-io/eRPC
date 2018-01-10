/**
 * @file appendentries.h
 * @brief Handlers for appendentries RPC
 */

#include "consensus.h"

#ifndef APPENDENTRIES_H
#define APPENDENTRIES_H

// The appendentries request sent via eRPC. Appendentries response is just
// Raft's msg_appendentries_response_t.
struct appendentries_req_t {
  int node_id;  // Node ID of the sender
  msg_appendentries_t ae;
  // If ae.n_entries > 0, the msg_entry_t structs are serialized here. Each
  // msg_entry_t struct's buf is placed immediately after the struct.
};

// appendentries request: node ID, msg_appendentries_t, [{size, buf}]
void appendentries_handler(erpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kRecvAeReq, erpc::rdtsc()));
  }

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t *buf = req_msgbuf->buf;

  // We'll use the msg_appendentries_t in the request MsgBuffer for
  // raft_recv_appendentries(), but we need to fill out its @entries first.
  auto *appendentries_req = reinterpret_cast<appendentries_req_t *>(buf);
  msg_appendentries_t ae = appendentries_req->ae;
  assert(ae.entries == nullptr);

  size_t n_entries = static_cast<size_t>(ae.n_entries);
  bool is_keepalive = (n_entries == 0);
  if (kAppVerbose) {
    printf("consensus: Received appendentries (%s) req from node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[appendentries_req->node_id].c_str(),
           erpc::get_formatted_time().c_str());
  }

  // Avoid malloc for n_entries < static_msg_entry_arr_size
  constexpr size_t static_msg_entry_arr_size = 16;
  msg_entry_t static_msg_entry_arr[static_msg_entry_arr_size];

  if (is_keepalive) {
    assert(req_msgbuf->get_data_size() == sizeof(appendentries_req_t));
  } else {
    // Non-keepalive appendentries requests contain app-defined log entries
    buf += sizeof(appendentries_req_t);
    if (n_entries <= static_msg_entry_arr_size) {
      ae.entries = static_msg_entry_arr;
    } else {
      ae.entries = new msg_entry_t[n_entries];  // Freed conditionally below
    }

    // Invariant: buf points to a msg_entry_t, followed by its buffer
    for (size_t i = 0; i < n_entries; i++) {
      ae.entries[i] = *(reinterpret_cast<msg_entry_t *>(buf));
      assert(ae.entries[i].data.buf == nullptr);
      assert(ae.entries[i].data.len == sizeof(client_req_t));

      // Copy out each SMR command buffer from the request msgbuf since the
      // msgbuf is valid for this function only.
      ae.entries[i].data.buf = c->server.rsm_cmd_buf_pool.alloc();
      memcpy(ae.entries[i].data.buf, buf + sizeof(msg_entry_t),
             sizeof(client_req_t));

      buf += (sizeof(msg_entry_t) + sizeof(client_req_t));
    }

    assert(buf == req_msgbuf->buf + req_msgbuf->get_data_size());
  }

  c->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                            sizeof(msg_appendentries_response_t));
  req_handle->prealloc_used = true;

  // This does a linear search, which is OK for a small number of Raft servers
  raft_node_t *requester_node =
      raft_get_node(c->server.raft, appendentries_req->node_id);
  assert(requester_node != nullptr);

  // The appendentries request and response structs need to valid only for
  // raft_recv_appendentries. The actual bufs in the appendentries request
  // need to be long-lived.
  auto *mar = reinterpret_cast<msg_appendentries_response_t *>(
      req_handle->pre_resp_msgbuf.buf);
  int e = raft_recv_appendentries(c->server.raft, requester_node, &ae, mar);
  _unused(e);
  assert(e == 0);

  if (n_entries > static_msg_entry_arr_size) {
    assert(ae.entries != nullptr && ae.entries != static_msg_entry_arr);
    delete[] ae.entries;
  }

  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kSendAeResp, erpc::rdtsc()));
  }
  c->rpc->enqueue_response(req_handle);
}

void appendentries_cont(erpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending appendentries message
static int __raft_send_appendentries(raft_server_t *, void *, raft_node_t *node,
                                     msg_appendentries_t *m) {
  assert(node != nullptr && m != nullptr);

  auto *conn = static_cast<connection_t *>(raft_node_get_udata(node));
  assert(conn != nullptr && conn->session_num >= 0 && conn->c != nullptr);

  AppContext *c = conn->c;
  assert(c->check_magic());

  bool is_keepalive = m->n_entries == 0;
  if (kAppVerbose) {
    printf("consensus: Sending appendentries (%s) to node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[raft_node_get_id(node)].c_str(),
           erpc::get_formatted_time().c_str());
  }

  if (!c->rpc->is_connected(conn->session_num)) {
    if (kAppVerbose) {
      printf("consensus: Cannot send appendentries on session %d.\n",
             conn->session_num);
    }
    return 0;
  }

  // Compute the request size. Keepalive appendentries requests do not have
  // a buffer, but they have an unused msg_entry_t (???).
  size_t req_size = sizeof(appendentries_req_t);
  for (size_t i = 0; i < static_cast<size_t>(m->n_entries); i++) {
    assert(m->entries[i].data.len == sizeof(client_req_t));
    req_size += sizeof(msg_entry_t) + sizeof(client_req_t);
  }

  erpc::rt_assert(req_size <= c->rpc->get_max_msg_size(),
                  "appendentries request too large for eRPC");

  raft_req_tag_t *rrt = c->server.raft_req_tag_pool.alloc();
  rrt->req_msgbuf = c->rpc->alloc_msg_buffer(req_size);
  erpc::rt_assert(rrt->req_msgbuf.buf != nullptr,
                  "Failed to allocate request MsgBuffer");

  rrt->resp_msgbuf =
      c->rpc->alloc_msg_buffer(sizeof(msg_appendentries_response_t));
  erpc::rt_assert(rrt->resp_msgbuf.buf != nullptr,
                  "Failed to allocate response MsgBuffer");

  rrt->node = node;

  // Fill in the appendentries request header
  auto *erpc_appendentries =
      reinterpret_cast<appendentries_req_t *>(rrt->req_msgbuf.buf);

  erpc_appendentries->node_id = c->server.node_id;
  erpc_appendentries->ae = *m;
  erpc_appendentries->ae.entries = nullptr;  // Was local pointer

  // Serialize each entry
  uint8_t *buf = rrt->req_msgbuf.buf + sizeof(appendentries_req_t);
  for (size_t i = 0; i < static_cast<size_t>(m->n_entries); i++) {
    auto *msg_entry = reinterpret_cast<msg_entry_t *>(buf);
    *msg_entry = m->entries[i];
    msg_entry->data.buf = nullptr;  // Was local pointer
    buf += sizeof(msg_entry_t);

    assert(m->entries[i].data.len == sizeof(client_req_t));
    memcpy(buf, m->entries[i].data.buf, sizeof(client_req_t));
    buf += sizeof(client_req_t);
  }

  assert(buf == rrt->req_msgbuf.buf + rrt->req_msgbuf.get_data_size());

  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kSendAeReq, erpc::rdtsc()));
  }

  c->rpc->enqueue_request(conn->session_num,
                          static_cast<uint8_t>(ReqType::kAppendEntries),
                          &rrt->req_msgbuf, &rrt->resp_msgbuf,
                          appendentries_cont, reinterpret_cast<size_t>(rrt));
  return 0;
}

void appendentries_cont(erpc::RespHandle *resp_handle, void *_context,
                        size_t tag) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *c = static_cast<AppContext *>(_context);
  assert(c->check_magic());

  if (kAppCollectTimeEntries) {
    c->server.time_entry_vec.push_back(
        TimeEntry(TimeEntryType::kRecvAeResp, erpc::rdtsc()));
  }

  auto *rrt = reinterpret_cast<raft_req_tag_t *>(tag);

  if (likely(rrt->resp_msgbuf.get_data_size() > 0)) {
    // The RPC was successful
    assert(rrt->resp_msgbuf.get_data_size() ==
           sizeof(msg_appendentries_response_t));

    if (kAppVerbose) {
      printf("consensus: Received appendentries response from node %s [%s].\n",
             node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
             erpc::get_formatted_time().c_str());
    }

    auto *msg_appendentries_response =
        reinterpret_cast<msg_appendentries_response_t *>(rrt->resp_msgbuf.buf);

    int e = raft_recv_appendentries_response(c->server.raft, rrt->node,
                                             msg_appendentries_response);
    _unused(e);
    assert(e == 0);
  } else {
    // This is a continuation-with-failure. Fall through and call
    // raft_periodic() again.
    printf("consensus: Appendentries RPC to node %s failed to complete [%s].\n",
           node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
           erpc::get_formatted_time().c_str());
  }

  c->rpc->free_msg_buffer(rrt->req_msgbuf);
  c->rpc->free_msg_buffer(rrt->resp_msgbuf);
  c->server.raft_req_tag_pool.free(rrt);

  c->rpc->release_response(resp_handle);
}

#endif
