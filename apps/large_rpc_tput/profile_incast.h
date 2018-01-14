#ifndef PROFILE_INCAST_H
#define PROFILE_INCAST_H

#include "large_rpc_tput.h"

// All threads create one session to process 0
size_t get_session_idx_func_incast(AppContext *, size_t) {
  erpc::rt_assert(FLAGS_process_id != 0, "Process 0 cannot send reqs.");
  return 0;
}

void connect_sessions_func_incast(AppContext *c) {
  assert(c->self_session_idx == SIZE_MAX);
  if (FLAGS_process_id == 0) return;

  // Allocate per-session info
  c->session_num_vec.resize(1);

  // Initiate connection for sessions
  fprintf(stderr,
          "large_rpc_tput: Thread %zu: Creating 1 session. Profile = incast.\n",
          c->thread_id);

  c->session_num_vec[0] = c->rpc->create_session(
      erpc::get_uri_for_process(0), static_cast<uint8_t>(c->thread_id));

  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session() failed");

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  c->rpc->set_session_rate_gbps(c->session_num_vec[0], FLAGS_session_gbps);
}

#endif
