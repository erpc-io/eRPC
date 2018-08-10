/**
 * @file appendentries.h
 * @brief Handlers for appendentries RPC
 */

#pragma once
#include "smr.h"

// The appendentries request sent over eRPC
struct app_ae_req_t {
  int node_id;  // Node ID of the sender
  msg_appendentries_t msg_ae;
  // If ae.n_entries > 0, the msg_entry_t structs are serialized here. Each
  // msg_entry_t struct's buf is placed immediately after the struct.
};

// The appendentries response sent over eRPC
struct app_ae_resp_t {
  msg_appendentries_response_t msg_ae_resp;
};

// appendentries request format is like so:
// node ID, msg_appendentries_t, [{size, buf}]
void appendentries_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  if (kAppTimeEnt) c->server.time_ents.emplace_back(TimeEntType::kRecvAeReq);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t *buf = req_msgbuf->buf;

  // We'll use the msg_appendentries_t in the request MsgBuffer for
  // raft_recv_appendentries(), but we need to fill out its @entries first.
  auto *ae_req = reinterpret_cast<app_ae_req_t *>(buf);
  msg_appendentries_t msg_ae = ae_req->msg_ae;
  assert(msg_ae.entries == nullptr);

  size_t n_entries = static_cast<size_t>(msg_ae.n_entries);
  bool is_keepalive = (n_entries == 0);
  if (kAppVerbose) {
    printf("smr: Received appendentries (%s) req from node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[ae_req->node_id].c_str(),
           erpc::get_formatted_time().c_str());
  }

  // Avoid malloc for n_entries < kStaticMsgEntryArrSize
  static constexpr size_t kStaticMsgEntryArrSize = 16;
  msg_entry_t static_msg_entry_arr[kStaticMsgEntryArrSize];

  if (is_keepalive) {
    assert(req_msgbuf->get_data_size() == sizeof(app_ae_req_t));
  } else {
    // Non-keepalive appendentries requests contain app-defined log entries
    buf += sizeof(app_ae_req_t);
    if (n_entries <= kStaticMsgEntryArrSize) {
      msg_ae.entries = static_msg_entry_arr;
    } else {
      msg_ae.entries = new msg_entry_t[n_entries];  // Freed conditionally below
    }

    // Invariant: buf points to a msg_entry_t, followed by its buffer
    for (size_t i = 0; i < n_entries; i++) {
      msg_ae.entries[i] = *(reinterpret_cast<msg_entry_t *>(buf));
      assert(msg_ae.entries[i].data.buf == nullptr);
      assert(msg_ae.entries[i].data.len == sizeof(client_req_t));

      // Copy out each SMR command buffer from the request msgbuf since the
      // msgbuf is valid for this function only.
      msg_ae.entries[i].data.buf = c->server.rsm_cmd_buf_pool.alloc();
      memcpy(msg_ae.entries[i].data.buf, buf + sizeof(msg_entry_t),
             sizeof(client_req_t));

      buf += (sizeof(msg_entry_t) + sizeof(client_req_t));
    }

    assert(buf == req_msgbuf->buf + req_msgbuf->get_data_size());
  }

  // This does a linear search, which is OK for a small number of Raft servers
  raft_node_t *requester_node = raft_get_node(c->server.raft, ae_req->node_id);
  erpc::rt_assert(requester_node != nullptr);

  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf;
  c->rpc->resize_msg_buffer(&resp_msgbuf, sizeof(app_ae_resp_t));
  req_handle->prealloc_used = true;

  // The appendentries request and response structs need to valid only for
  // raft_recv_appendentries. The actual bufs in the appendentries request
  // need to be long-lived.
  int e = raft_recv_appendentries(
      c->server.raft, requester_node, &msg_ae,
      &reinterpret_cast<app_ae_resp_t *>(resp_msgbuf.buf)->msg_ae_resp);
  erpc::rt_assert(e == 0);

  if (n_entries > kStaticMsgEntryArrSize) {
    assert(msg_ae.entries != nullptr && msg_ae.entries != static_msg_entry_arr);
    delete[] msg_ae.entries;
  }

  if (kAppTimeEnt) c->server.time_ents.emplace_back(TimeEntType::kSendAeResp);
  c->rpc->enqueue_response(req_handle);
}

void appendentries_cont(erpc::RespHandle *, void *, size_t);  // Fwd decl

// Raft callback for sending appendentries message
static int __raft_send_appendentries(raft_server_t *, void *, raft_node_t *node,
                                     msg_appendentries_t *msg_ae) {
  auto *conn = static_cast<connection_t *>(raft_node_get_udata(node));
  AppContext *c = conn->c;

  bool is_keepalive = (msg_ae->n_entries == 0);
  if (kAppVerbose) {
    printf("smr: Sending appendentries (%s) to node %s [%s].\n",
           is_keepalive ? "keepalive" : "non-keepalive",
           node_id_to_name_map[raft_node_get_id(node)].c_str(),
           erpc::get_formatted_time().c_str());
  }

  if (!c->rpc->is_connected(conn->session_num)) {
    if (kAppVerbose) {
      printf("smr: Cannot send ae req on session %d.\n", conn->session_num);
    }
    return 0;
  }

  // Compute the request size. Keepalive appendentries requests do not have
  // a buffer, but they have an unused msg_entry_t (???).
  size_t req_size = sizeof(app_ae_req_t);
  for (size_t i = 0; i < static_cast<size_t>(msg_ae->n_entries); i++) {
    assert(msg_ae->entries[i].data.len == sizeof(client_req_t));
    req_size += sizeof(msg_entry_t) + sizeof(client_req_t);
  }

  erpc::rt_assert(req_size <= c->rpc->get_max_msg_size());

  raft_req_tag_t *rrt = c->server.raft_req_tag_pool.alloc();
  rrt->req_msgbuf = c->rpc->alloc_msg_buffer(req_size);
  erpc::rt_assert(rrt->req_msgbuf.buf != nullptr);

  rrt->resp_msgbuf = c->rpc->alloc_msg_buffer(sizeof(app_ae_resp_t));
  erpc::rt_assert(rrt->resp_msgbuf.buf != nullptr);

  rrt->node = node;

  // Fill in the appendentries request header
  auto *ae_req = reinterpret_cast<app_ae_req_t *>(rrt->req_msgbuf.buf);

  ae_req->node_id = c->server.node_id;
  ae_req->msg_ae = *msg_ae;
  ae_req->msg_ae.entries = nullptr;  // Was local pointer

  // Serialize each entry
  uint8_t *buf = rrt->req_msgbuf.buf + sizeof(app_ae_req_t);
  for (size_t i = 0; i < static_cast<size_t>(msg_ae->n_entries); i++) {
    auto *msg_entry = reinterpret_cast<msg_entry_t *>(buf);
    *msg_entry = msg_ae->entries[i];
    msg_entry->data.buf = nullptr;  // Was local pointer
    buf += sizeof(msg_entry_t);

    assert(msg_ae->entries[i].data.len == sizeof(client_req_t));
    memcpy(buf, msg_ae->entries[i].data.buf, sizeof(client_req_t));
    buf += sizeof(client_req_t);
  }

  assert(buf == rrt->req_msgbuf.buf + rrt->req_msgbuf.get_data_size());
  if (kAppTimeEnt) c->server.time_ents.emplace_back(TimeEntType::kSendAeReq);
  c->rpc->enqueue_request(conn->session_num,
                          static_cast<uint8_t>(ReqType::kAppendEntries),
                          &rrt->req_msgbuf, &rrt->resp_msgbuf,
                          appendentries_cont, reinterpret_cast<size_t>(rrt));
  return 0;
}

void appendentries_cont(erpc::RespHandle *resp_handle, void *_context,
                        size_t tag) {
  auto *c = static_cast<AppContext *>(_context);
  if (kAppTimeEnt) c->server.time_ents.emplace_back(TimeEntType::kRecvAeResp);
  auto *rrt = reinterpret_cast<raft_req_tag_t *>(tag);

  if (likely(rrt->resp_msgbuf.get_data_size() > 0)) {
    // The RPC was successful
    assert(rrt->resp_msgbuf.get_data_size() == sizeof(app_ae_resp_t));
    if (kAppVerbose) {
      printf("smr: Received appendentries response from node %s [%s].\n",
             node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
             erpc::get_formatted_time().c_str());
    }

    int e = raft_recv_appendentries_response(
        c->server.raft, rrt->node,
        &reinterpret_cast<app_ae_resp_t *>(rrt->resp_msgbuf.buf)->msg_ae_resp);
    erpc::rt_assert(e == 0);
  } else {
    // The RPC failed. Fall through and call raft_periodic() again.
    printf("smr: Appendentries RPC to node %s failed to complete [%s].\n",
           node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
           erpc::get_formatted_time().c_str());
  }

  c->rpc->free_msg_buffer(rrt->req_msgbuf);
  c->rpc->free_msg_buffer(rrt->resp_msgbuf);
  c->server.raft_req_tag_pool.free(rrt);
  c->rpc->release_response(resp_handle);
}
