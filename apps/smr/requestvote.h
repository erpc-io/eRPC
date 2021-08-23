/**
 * @file requestvote.h
 * @brief Handlers for requestvote RPC
 */

#pragma once
#include "smr.h"

// With eRPC, there is currently no way for an RPC server to access connection
// data for a request, so the client's Raft node ID is included in the request.
struct app_requestvote_t {
  int node_id;
  msg_requestvote_t msg_rv;
};

// Return a string representation of a requestvote request message
static std::string msg_requestvote_string(msg_requestvote_t *msg_rv) {
  std::ostringstream ret;
  ret << "[candidate_id " << node_id_to_name_map[msg_rv->candidate_id] << ", "
      << "last log idx " << std::to_string(msg_rv->last_log_idx) << ", "
      << "last log term " << std::to_string(msg_rv->last_log_term) << ", "
      << "term " << std::to_string(msg_rv->term) << "]";
  return ret.str();
}

// Return a string representation of a requestvote request message
static std::string msg_requestvote_response_string(
    msg_requestvote_response_t *msg_rv_resp) {
  std::ostringstream ret;
  ret << "[term " << std::to_string(msg_rv_resp->term) << ", "
      << "vote granted " << std::to_string(msg_rv_resp->vote_granted) << "]";
  return ret.str();
}

void requestvote_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(app_requestvote_t));

  auto *rv_req = reinterpret_cast<app_requestvote_t *>(req_msgbuf->buf_);
  printf("smr: Received requestvote request from %s: %s [%s].\n",
         node_id_to_name_map[rv_req->node_id].c_str(),
         msg_requestvote_string(&rv_req->msg_rv).c_str(),
         erpc::get_formatted_time().c_str());

  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf_;
  c->rpc->resize_msg_buffer(&resp_msgbuf, sizeof(msg_requestvote_response_t));

  auto *rv_resp =
      reinterpret_cast<msg_requestvote_response_t *>(resp_msgbuf.buf_);

  // rv_req->msg_rv is valid only for the duration of this handler, which is OK
  // as msg_requestvote_t does not contain any dynamically allocated members.
  int e = raft_recv_requestvote(c->server.raft,
                                raft_get_node(c->server.raft, rv_req->node_id),
                                &rv_req->msg_rv, rv_resp);
  erpc::rt_assert(e == 0, "raft_recv_requestvote failed");

  printf("smr: Sending requestvote response to %s: %s [%s].\n",
         node_id_to_name_map[rv_req->node_id].c_str(),
         msg_requestvote_response_string(rv_resp).c_str(),
         erpc::get_formatted_time().c_str());

  c->rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void requestvote_cont(void *, void *);  // Fwd decl

// Raft callback for sending requestvote request
static int smr_raft_send_requestvote_cb(raft_server_t *, void *,
                                        raft_node_t *node,
                                        msg_requestvote_t *msg_rv) {
  auto *conn = static_cast<connection_t *>(raft_node_get_udata(node));
  AppContext *c = conn->c;

  if (!c->rpc->is_connected(conn->session_num)) {
    printf("smr: Cannot send requestvote request (disconnected).\n");
    return 0;
  }

  printf("smr: Sending requestvote request to node %s [%s].\n",
         node_id_to_name_map[raft_node_get_id(node)].c_str(),
         erpc::get_formatted_time().c_str());

  raft_req_tag_t *rrt = c->server.raft_req_tag_pool.alloc();
  rrt->req_msgbuf = c->rpc->alloc_msg_buffer_or_die(sizeof(app_requestvote_t));
  rrt->resp_msgbuf = c->rpc->alloc_msg_buffer_or_die(sizeof(app_requestvote_t));
  rrt->node = node;

  auto *rv_req = reinterpret_cast<app_requestvote_t *>(rrt->req_msgbuf.buf_);
  rv_req->node_id = c->server.node_id;
  rv_req->msg_rv = *msg_rv;

  c->rpc->enqueue_request(conn->session_num,
                          static_cast<uint8_t>(ReqType::kRequestVote),
                          &rrt->req_msgbuf, &rrt->resp_msgbuf, requestvote_cont,
                          reinterpret_cast<void *>(rrt));
  return 0;
}

void requestvote_cont(void *_context, void *_tag) {
  auto *c = static_cast<AppContext *>(_context);
  auto *rrt = reinterpret_cast<raft_req_tag_t *>(_tag);

  auto *msg_rv_resp =
      reinterpret_cast<msg_requestvote_response_t *>(rrt->resp_msgbuf.buf_);

  if (likely(rrt->resp_msgbuf.get_data_size() > 0)) {
    // The RPC was successful
    printf("smr: Received requestvote response from node %s: %s [%s].\n",
           node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
           msg_requestvote_response_string(msg_rv_resp).c_str(),
           erpc::get_formatted_time().c_str());

    int e =
        raft_recv_requestvote_response(c->server.raft, rrt->node, msg_rv_resp);
    // XXX: Doc says shutdown if e != 0
    erpc::rt_assert(
        e == 0, "raft_recv_requestvote_response failed");
  } else {
    // This is a continuation-with-failure
    printf("smr: Requestvote RPC to node %s failed to complete [%s].\n",
           node_id_to_name_map[raft_node_get_id(rrt->node)].c_str(),
           erpc::get_formatted_time().c_str());
  }

  c->rpc->free_msg_buffer(rrt->req_msgbuf);
  c->rpc->free_msg_buffer(rrt->resp_msgbuf);
  c->server.raft_req_tag_pool.free(rrt);
}
