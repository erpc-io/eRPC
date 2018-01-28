#ifndef PROFILE_INCAST_H
#define PROFILE_INCAST_H

#include "large_rpc_tput.h"

size_t get_session_idx_func_incast(AppContext *) {
  erpc::rt_assert(FLAGS_process_id != 0, "Process 0 cannot send reqs.");
  return 0;
}

void connect_sessions_func_incast(AppContext *c) {
  // All non-zero processes create one session to process #0
  if (FLAGS_process_id == 0) return;

  size_t global_thread_id =
      FLAGS_process_id * FLAGS_num_proc_other_threads + c->thread_id;
  size_t rem_tid = global_thread_id % FLAGS_num_proc_0_threads;

  c->session_num_vec.resize(1);

  printf(
      "large_rpc_tput: Thread %zu: Creating 1 session to proc 0, thread %zu.\n",
      c->thread_id, rem_tid);

  c->session_num_vec[0] =
      c->rpc->create_session(erpc::get_uri_for_process(0), rem_tid);
  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session() failed");

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  if (FLAGS_throttle == 1) {
    erpc::Timely *timely_0 = c->rpc->get_timely(c->session_num_vec[0]);
    double num_flows = (FLAGS_num_processes - 1) * FLAGS_num_proc_other_threads;
    double fair_share = erpc::kBandwidth / num_flows;

    timely_0->rate = fair_share * FLAGS_throttle_fraction;
  }
}

#endif
