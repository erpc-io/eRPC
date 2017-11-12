#ifndef PROFILE_TIMELY_SMALL_H
#define PROFILE_TIMELY_SMALL_H

#include "large_rpc_tput.h"

// All threads create one session to machine 0
size_t get_session_idx_func_timely_small(AppContext *, size_t) {
  erpc::rt_assert(FLAGS_machine_id != 0, "Machine 0 cannot send reqs.");
  return 0;
}

void connect_sessions_func_timely_small(AppContext *c) {
  assert(c->self_session_idx == std::numeric_limits<size_t>::max());

  if (FLAGS_machine_id == 0) return;

  // Allocate per-session info
  c->session_num_vec.resize(1);
  c->session_num_vec[0] = -1;

  // Initiate connection for sessions
  fprintf(stderr,
          "large_rpc_tput: Thread %zu: Creating 1 session. "
          "Profile = 'timely_small'.\n",
          c->thread_id);

  std::string hostname = get_hostname_for_machine(0);

  c->session_num_vec[0] = c->rpc->create_session(
      hostname, static_cast<uint8_t>(c->thread_id), kAppPhyPort);

  if (c->session_num_vec[0] < 0) {
    throw std::runtime_error("Failed to create session.");
  }

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

#endif
