#include "util/latency.h"
#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;    // Print debug info on datapath
static constexpr double kAppLatFac = 10.0;    // Precision factor for latency
static constexpr size_t kAppReqType = 1;      // eRPC request type

DEFINE_uint64(msg_size, 0, "Request and response size");

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  size_t start_tsc;
  erpc::Latency latency;
  erpc::MsgBuffer req_msgbuf, resp_msgbuf;
  ~AppContext() {}
};

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  _unused(req_msgbuf);
  assert(req_msgbuf->get_data_size() == FLAGS_msg_size);

  auto *c = static_cast<AppContext *>(_context);
  req_handle->prealloc_used = true;
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                 FLAGS_msg_size);
  c->rpc->enqueue_response(req_handle);
}

void server_func(erpc::Nexus *nexus) {
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  AppContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0 /* tid */,
                                  basic_sm_handler, phy_port);
  c.rpc = &rpc;

  while (true) {
    rpc.run_event_loop(1000);
    if (ctrl_c_pressed == 1) break;
  }
}

void connect_session(AppContext &c) {
  std::string server_uri = erpc::get_uri_for_process(0);
  if (FLAGS_sm_verbose == 1) {
    printf("Process %zu: Creating session to %s.\n", FLAGS_process_id,
           server_uri.c_str());
  }

  int session_num = c.rpc->create_session(server_uri, 0 /* tid */);
  erpc::rt_assert(session_num >= 0, "Failed to create session");
  c.session_num_vec.push_back(session_num);

  while (c.num_sm_resps != 1) {
    c.rpc->run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) return;
  }
}

void app_cont_func(erpc::RespHandle *, void *, size_t);
inline void send_req(AppContext &c) {
  c.start_tsc = erpc::rdtsc();
  c.rpc->enqueue_request(c.session_num_vec[0], kAppReqType, &c.req_msgbuf,
                         &c.resp_msgbuf, app_cont_func, 0);
}

void app_cont_func(erpc::RespHandle *resp_handle, void *_context, size_t) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf->get_data_size() == FLAGS_msg_size);
  _unused(resp_msgbuf);

  auto *c = static_cast<AppContext *>(_context);
  c->rpc->release_response(resp_handle);

  double req_lat_us =
      erpc::to_usec(erpc::rdtsc() - c->start_tsc, c->rpc->get_freq_ghz());
  c->latency.update(static_cast<size_t>(req_lat_us * kAppLatFac));

  send_req(*c);
}

void client_func(erpc::Nexus *nexus) {
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  AppContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0,
                                  basic_sm_handler, phy_port);

  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  c.req_msgbuf = rpc.alloc_msg_buffer_or_die(FLAGS_msg_size);
  c.resp_msgbuf = rpc.alloc_msg_buffer_or_die(FLAGS_msg_size);

  connect_session(c);

  printf("Process %zu: Session connected. Starting work.\n", FLAGS_process_id);
  printf("lat_us_50 lat_us_99 lat_us_99.9\n");

  send_req(c);
  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) break;
    printf("%.1f %.1f %.1f\n", c.latency.perc(.5) / kAppLatFac,
           c.latency.perc(.99) / kAppLatFac, c.latency.perc(.999) / kAppLatFac);
    c.latency.reset();
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  auto t =
      std::thread(FLAGS_process_id == 0 ? server_func : client_func, &nexus);
  erpc::bind_to_core(t, FLAGS_numa_node, 0);
  t.join();
}
