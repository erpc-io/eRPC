#ifndef PROFILE_INCAST_H
#define PROFILE_INCAST_H

#include "large_rpc_tput.h"

size_t get_session_idx_func_incast(AppContext *) {
  erpc::rt_assert(FLAGS_process_id != 0, "Process 0 cannot send reqs.");
  return 0;
}

void connect_sessions_func_incast(AppContext *c) {
  if (FLAGS_process_id == 0) return;

  // All non-zero processes create one session to process #0
  c->session_num_vec.resize(1);
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

  if (FLAGS_throttle == 1) {
    erpc::Timely *timely_0 = c->rpc->get_timely(c->session_num_vec[0]);
    double num_flows = (FLAGS_num_processes - 1) * FLAGS_num_threads;
    double fair_share = erpc::kBandwidth / num_flows;

    timely_0->rate = fair_share * FLAGS_throttle_fraction;
  }
}

#endif
