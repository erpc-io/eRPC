#ifndef LARGE_RPC_TPUT_H
#define LARGE_RPC_TPUT_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "mt_index_api.h"
#include "rpc.h"
#include "util/latency.h"
#include "util/misc.h"

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppPointReqType = 1;
static constexpr size_t kAppRangeReqType = 2;
static constexpr size_t kMaxReqWindow = 8;  // Max pending reqs per client

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Flags
DEFINE_uint64(num_server_fg_threads, 0, "Number of server foreground threads");
DEFINE_uint64(num_server_bg_threads, 0, "Number of server background threads");
DEFINE_uint64(num_client_threads, 0, "Number of client threads");
DEFINE_uint64(req_window, 0, "Outstanding requests per client thread");
DEFINE_uint64(num_keys, 0, "Number of keys in the server's Masstree");

static bool validate_req_window(const char *, uint64_t req_window) {
  return req_window <= kMaxReqWindow;
}
DEFINE_validator(req_window, &validate_req_window);

// Return true iff this machine is the one server
bool is_server() { return FLAGS_machine_id == 0; };

enum class ReqType : size_t { kPoint, kRange };
struct req_t {
  ReqType req_type;
  union {
    struct {
      size_t key;
    } point_req;

    struct {
      size_t key;
      size_t range;  // The max number of keys after key to sum up
    } range_req;
  }
};

enum class RespType : size_t { kFound, kNotFound };
struct resp_t {
  RespType resp_type;
  union {
    size_t value;      // The value for point GETs
    size_t range_sum;  // The range sum for range queries
  }
};

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  MtIndex *mt_index = nullptr;  // The Masstree index. Valid at server only.
  ERpc::Latency point_latency;  // Latency of point requests (factor = 10)
  ERpc::Latency range_latency;  // Latency of point requests (factor = .1)

  uint64_t req_ts[kMaxReqWindow];  // Per-request timestamps
  ERpc::MsgBuffer req_msgbuf[kMaxReqWindow];
  ERpc::MsgBuffer resp_msgbuf[kMaxReqWindow];

  size_t num_resps_tot = 0;  // Total responses received (range & point)
};

// Allocate request and response MsgBuffers
void alloc_req_resp_msg_buffers(AppContext *c) {
  assert(c != nullptr);
  assert(c->rpc != nullptr);

  for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_req_window; msgbuf_idx++) {
    c->req_msgbuf[msgbuf_idx] = c->rpc->alloc_msg_buffer(sizeof(req_t));
    ERpc::rt_assert(c->req_msgbuf[msgbuf_idx].buf != nullptr, "Alloc failed");

    c->resp_msgbuf[msgbuf_idx] = c->rpc->alloc_msg_buffer(sizeof(resp_t));
    ERpc::rt_assert(c->resp_msgbuf[msgbuf_idx].buf != nullptr, "Alloc failed");
  }
}

#endif
