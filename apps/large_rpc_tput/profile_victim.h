#ifndef PROFILE_VICTIM_H
#define PROFILE_VICTIM_H

#include "large_rpc_tput.h"

bool process_in_victim_pair() {
  return (FLAGS_process_id == FLAGS_num_processes - 1) ||
         (FLAGS_process_id == FLAGS_num_processes - 2);
}

size_t get_session_idx_func_victim(AppContext *, size_t resp_session_idx) {
  erpc::rt_assert(FLAGS_process_id != 0, "Process 0 cannot send reqs.");

  // During initialization, alternate between process 0 and the other victim
  static size_t initial_call_index = 0;
  if (unlikely(resp_session_idx == SIZE_MAX)) {
    if (process_in_victim_pair()) {
      size_t ret = initial_call_index % 2;
      initial_call_index++;
      return ret;
    } else {
      return 0;
    }
  }

  // Non-initialization mode
  if (process_in_victim_pair()) {
    return resp_session_idx;
  } else {
    return 0;
  }
}

void connect_sessions_func_victim(AppContext *c) {
  assert(c->self_session_idx == SIZE_MAX);

  if (FLAGS_process_id == 0) return;

  // Allocate per-session info
  c->session_num_vec.resize(2);
  c->session_num_vec[0] = -1;
  c->session_num_vec[1] = -1;

  // Initiate connection for sessions
  if (process_in_victim_pair()) {
    // Create two session: to process 0 and to the victim peer
    printf("large_rpc_tput: Thread %zu: Creating 2 session. Profile 'victim'.",
           c->thread_id);

    // Process 0
    c->session_num_vec[0] = c->rpc->create_session(
        erpc::get_uri_for_process(0), static_cast<uint8_t>(c->thread_id));
    erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session failed.");

    // Victim peer
    size_t other_victim = FLAGS_process_id == FLAGS_num_processes - 1
                              ? FLAGS_num_processes - 2
                              : FLAGS_num_processes - 1;
    c->session_num_vec[1] =
        c->rpc->create_session(erpc::get_uri_for_process(other_victim),
                               static_cast<uint8_t>(c->thread_id));
    erpc::rt_assert(c->session_num_vec[1] >= 0, "create_session failed.");

    while (c->num_sm_resps != 2) {
      c->rpc->run_event_loop(200);  // 200 milliseconds
      if (ctrl_c_pressed == 1) return;
    }
  } else {
    // Create one session to process 0
    c->session_num_vec.resize(1);
    c->session_num_vec[0] = -1;

    // Initiate connection for sessions
    printf(
        "large_rpc_tput: Thread %zu: Creating 1 session. Profile = 'victim'.\n",
        c->thread_id);

    c->session_num_vec[0] = c->rpc->create_session(
        erpc::get_uri_for_process(0), static_cast<uint8_t>(c->thread_id));
    erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session failed.");

    while (c->num_sm_resps != 1) {
      c->rpc->run_event_loop(200);  // 200 milliseconds
      if (ctrl_c_pressed == 1) return;
    }
  }
}

#endif
