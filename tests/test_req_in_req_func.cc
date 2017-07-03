/**
 * @file test_nested_rpc.cc
 * @brief Test issuing requests from within request handlers. This uses a
 * primary-backup setup, where the client sends requests to the primary,
 * which completes an RPC with *one* of the backups before replying.
 */
#include "test_basics.h"

// Set to true if the request handler or continuation at the primary or backup
// should run in the background.
bool primary_bg, backup_bg;

static constexpr size_t kAppNumReqs = 33;
static_assert(kAppNumReqs > Session::kSessionReqWindow, "");

/// Request type used for client to primary
static constexpr uint8_t kAppReqTypeCP = kAppReqType + 1;

/// Request type used for primary to backup
static constexpr uint8_t kAppReqTypePB = kAppReqType + 2;

/// Per-request info maintained at the primary
class PrimaryReqInfo {
 public:
  size_t req_size_cp;        ///< Size of client-to-primary request
  ReqHandle *req_handle_cp;  ///< Handle for client-to-primary request
  MsgBuffer req_msgbuf_pb;   ///< MsgBuffer for primary-to-backup request
  MsgBuffer resp_msgbuf_pb;  ///< MsgBuffer for primary-to-backup response
  size_t etid;               ///< ERpc thread ID in the request handler

  PrimaryReqInfo(size_t req_size_cp, ReqHandle *req_handle_cp, size_t etid)
      : req_size_cp(req_size_cp), req_handle_cp(req_handle_cp), etid(etid) {}
};

union tag_t {
  PrimaryReqInfo *srv_req_info_ptr;
  struct {
    uint16_t req_i;
    uint16_t msgbuf_i;
    uint32_t req_size;
  };
  size_t tag;

  tag_t(PrimaryReqInfo *srv_req_info_ptr)
      : srv_req_info_ptr(srv_req_info_ptr) {}
  tag_t(uint16_t req_i, uint16_t msgbuf_i, uint32_t req_size)
      : req_i(req_i), msgbuf_i(msgbuf_i), req_size(req_size) {}
  tag_t(size_t tag) : tag(tag) {}
};
static_assert(sizeof(tag_t) == sizeof(size_t), "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand;
  MsgBuffer req_msgbuf[Session::kSessionReqWindow];
  MsgBuffer resp_msgbuf[Session::kSessionReqWindow];
  size_t num_reqs_sent = 0;
};

///
/// Server-side code
///

// Forward declaration
void primary_cont_func(RespHandle *, void *, size_t);

/// The primary's request handler for client-to-primary requests. Forwards the
/// received request to one of the backup servers.
void req_handler_cp(ReqHandle *req_handle_cp, void *_context) {
  assert(req_handle_cp != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);
  ASSERT_EQ(context->rpc->in_background(), primary_bg);

  const MsgBuffer *req_msgbuf_cp = req_handle_cp->get_req_msgbuf();
  size_t req_size_cp = req_msgbuf_cp->get_data_size();

  test_printf("Primary [Rpc %u]: Received request of length %zu.\n",
              context->rpc->get_rpc_id(), req_size_cp);

  // Record info for the request that we are now sending to server #1
  PrimaryReqInfo *srv_req_info =
      new PrimaryReqInfo(req_size_cp, req_handle_cp, context->rpc->get_etid());

  MsgBuffer &req_msgbuf_pb = srv_req_info->req_msgbuf_pb;
  req_msgbuf_pb = context->rpc->alloc_msg_buffer(req_size_cp);
  assert(req_msgbuf_pb.buf != nullptr);

  MsgBuffer &resp_msgbuf_pb = srv_req_info->resp_msgbuf_pb;
  resp_msgbuf_pb = context->rpc->alloc_msg_buffer(req_size_cp);
  assert(resp_msgbuf_pb.buf != nullptr);

  // Request to server #1 = client-to-server request + 1
  for (size_t i = 0; i < req_size_cp; i++) {
    req_msgbuf_pb.buf[i] = req_msgbuf_cp->buf[i] + 1;
  }

  tag_t tag(srv_req_info);  // Save the request info pointer in the tag

  int ret = context->rpc->enqueue_request(
      context->session_num_arr[1], kAppReqTypePB, &req_msgbuf_pb,
      &resp_msgbuf_pb, primary_cont_func, tag.tag);
  _unused(ret);
  assert(ret == 0);
}

/// The backups' request handler for primary-to-backup to requests. Echoes the
/// received request back to the primary.
void req_handler_pb(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);
  ASSERT_EQ(context->rpc->in_background(), backup_bg);

  const MsgBuffer *req_msgbuf_pb = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf_pb->get_data_size();

  test_printf("Backup [Rpc %u]: Received request of length %zu.\n",
              context->rpc->get_rpc_id(), req_size);

  // eRPC will free dyn_resp_msgbuf
  req_handle->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(req_size);
  assert(req_handle->dyn_resp_msgbuf.buf != nullptr);

  // Response to primary = request + 1
  for (size_t i = 0; i < req_size; i++) {
    req_handle->dyn_resp_msgbuf.buf[i] = req_msgbuf_pb->buf[i] + 1;
  }

  req_handle->prealloc_used = false;
  context->rpc->enqueue_response(req_handle);
}

/// The primary's continuation function when it gets a response from a backup
void primary_cont_func(RespHandle *resp_handle_pb, void *_context,
                       size_t _tag) {
  assert(resp_handle_pb != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);
  ASSERT_EQ(context->rpc->in_background(), primary_bg);

  const MsgBuffer *resp_msgbuf_pb = resp_handle_pb->get_resp_msgbuf();
  test_printf("Primary [Rpc %u]: Received response of length %zu.\n",
              context->rpc->get_rpc_id(), resp_msgbuf_pb->get_data_size());

  // Check that we're still running in the same thread as for the
  // client-to-primary request
  tag_t tag(_tag);
  PrimaryReqInfo *srv_req_info = tag.srv_req_info_ptr;
  assert(srv_req_info->etid == context->rpc->get_etid());

  // Extract the request info
  size_t req_size_cp = srv_req_info->req_size_cp;
  ReqHandle *req_handle_cp = srv_req_info->req_handle_cp;
  MsgBuffer &req_msgbuf_pb = srv_req_info->req_msgbuf_pb;

  assert(resp_msgbuf_pb->get_data_size() == req_size_cp);

  // Check the response from server #1
  for (size_t i = 0; i < req_size_cp; i++) {
    assert(req_msgbuf_pb.buf[i] + 1 == resp_msgbuf_pb->buf[i]);
  }

  // eRPC will free dyn_resp_msgbuf
  req_handle_cp->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(req_size_cp);
  assert(req_handle_cp->dyn_resp_msgbuf.buf != nullptr);

  // Response to client = server-to-server response + 1
  for (size_t i = 0; i < req_size_cp; i++) {
    req_handle_cp->dyn_resp_msgbuf.buf[i] = resp_msgbuf_pb->buf[i] + 1;
  }

  // Free resources of the server-to-server request
  context->rpc->free_msg_buffer(req_msgbuf_pb);
  delete srv_req_info;

  // Release the server-server response
  context->rpc->release_response(resp_handle_pb);

  // Send response to the client
  req_handle_cp->prealloc_used = false;
  context->rpc->enqueue_response(req_handle_cp);
}

///
/// Client-side code
///
void client_cont_func(RespHandle *, void *, size_t);  // Forward declaration

/// Enqueue a request to server 0 using the request MsgBuffer index msgbuf_i
void client_request_helper(AppContext *context, size_t msgbuf_i) {
  assert(context != nullptr && msgbuf_i < Session::kSessionReqWindow);

  size_t req_size = get_rand_msg_size(&context->fast_rand,
                                      context->rpc->get_max_data_per_pkt(),
                                      context->rpc->get_max_msg_size());

  context->rpc->resize_msg_buffer(&context->req_msgbuf[msgbuf_i], req_size);

  // Fill in all the bytes of the request MsgBuffer with msgbuf_i
  MsgBuffer &req_msgbuf = context->req_msgbuf[msgbuf_i];
  for (size_t i = 0; i < req_size; i++) {
    req_msgbuf.buf[i] = static_cast<uint8_t>(msgbuf_i);
  }

  tag_t tag(static_cast<uint16_t>(context->num_reqs_sent),
            static_cast<uint16_t>(msgbuf_i), static_cast<uint32_t>(req_size));
  test_printf("Client: Sending request %zu of size %zu\n",
              context->num_reqs_sent, req_size);

  int ret = context->rpc->enqueue_request(
      context->session_num_arr[0], kAppReqTypeCP, &req_msgbuf,
      &context->resp_msgbuf[msgbuf_i], client_cont_func, tag.tag);
  _unused(ret);
  assert(ret == 0);

  context->num_reqs_sent++;
}

void client_cont_func(RespHandle *resp_handle, void *_context, size_t _tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  assert(context->is_client);

  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();

  // Extract info from tag
  tag_t tag(_tag);
  size_t req_size = static_cast<size_t>(tag.req_size);
  size_t msgbuf_i = static_cast<size_t>(tag.msgbuf_i);

  test_printf("Client: Received response for req %u, length = %zu.\n",
              tag.req_i, resp_msgbuf->get_data_size());

  // Check the response
  ASSERT_EQ(resp_msgbuf->get_data_size(), req_size);
  for (size_t i = 0; i < req_size; i++) {
    ASSERT_EQ(resp_msgbuf->buf[i], static_cast<uint8_t>(msgbuf_i) + 3);
  }

  context->num_rpc_resps++;
  context->rpc->release_response(resp_handle);

  if (context->num_reqs_sent < kAppNumReqs) {
    client_request_helper(context, static_cast<tag_t>(tag).msgbuf_i);
  }
}

void client_thread(Nexus<IBTransport> *nexus, size_t num_sessions) {
  // Create the Rpc and connect the sessions
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;

  // Start by filling the request window
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    context.req_msgbuf[i] =
        rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
    assert(context.req_msgbuf[i].buf != nullptr);

    context.resp_msgbuf[i] =
        rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
    assert(context.resp_msgbuf[i].buf != nullptr);

    client_request_helper(&context, i);
  }

  wait_for_rpc_resps_or_timeout(context, kAppNumReqs, nexus->freq_ghz);
  assert(context.num_rpc_resps == kAppNumReqs);

  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    rpc->free_msg_buffer(context.req_msgbuf[i]);
  }

  // Disconnect the sessions
  context.num_sm_resps = 0;
  for (size_t i = 0; i < num_sessions; i++) {
    rpc->destroy_session(context.session_num_arr[i]);
  }
  wait_for_sm_resps_or_timeout(context, num_sessions, nexus->freq_ghz);

  // Free resources
  delete rpc;
  client_done = true;
}

/// 1 primary, 1 backup, both in foreground
TEST(Base, BothInForeground) {
  primary_bg = false;
  backup_bg = false;

  auto reg_info_vec = {
      ReqFuncRegInfo(kAppReqTypeCP, req_handler_cp, ReqFuncType::kForeground),
      ReqFuncRegInfo(kAppReqTypePB, req_handler_pb, ReqFuncType::kForeground)};

  // 2 client sessions (=> 2 server threads), 0 background threads
  launch_server_client_threads(2, 0, client_thread, reg_info_vec,
                               ConnectServers::kTrue, 0.0);
}

/// 1 primary, 1 backup, primary in background
TEST(Base, PrimaryInBackground) {
  primary_bg = true;
  backup_bg = false;

  auto reg_info_vec = {
      ReqFuncRegInfo(kAppReqTypeCP, req_handler_cp, ReqFuncType::kBackground),
      ReqFuncRegInfo(kAppReqTypePB, req_handler_pb, ReqFuncType::kForeground)};

  // 2 client sessions (=> 2 server threads), 3 background threads
  launch_server_client_threads(2, 1, client_thread, reg_info_vec,
                               ConnectServers::kTrue, 0.0);
}

/// 1 primary, 1 backup, both in background
TEST(Base, BothInBackground) {
  primary_bg = true;
  backup_bg = true;

  auto reg_info_vec = {
      ReqFuncRegInfo(kAppReqTypeCP, req_handler_cp, ReqFuncType::kBackground),
      ReqFuncRegInfo(kAppReqTypePB, req_handler_pb, ReqFuncType::kBackground)};

  // 2 client sessions (=> 2 server threads), 3 background threads
  launch_server_client_threads(2, 3, client_thread, reg_info_vec,
                               ConnectServers::kTrue, 0.0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
