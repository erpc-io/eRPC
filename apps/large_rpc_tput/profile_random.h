#ifndef PROFILE_RANDOM_H
#define PROFILE_RANDOM_H

#include "large_rpc_tput.h"

size_t get_session_idx_func_random(AppContext *c, size_t) {
  size_t session_vec_size = c->session_num_vec.size();

  // We don't need Lemire's trick bc messages are large
  size_t rand_session_idx = c->fastrand.next_u32() % session_vec_size;
  while (rand_session_idx == c->self_session_idx) {
    rand_session_idx = c->fastrand.next_u32() % session_vec_size;
  }

  return rand_session_idx;
}

void connect_sessions_func_random(AppContext *c) {
  c->self_session_idx = FLAGS_process_id * FLAGS_num_threads + c->thread_id;

  // Allocate per-session info
  size_t num_sessions = FLAGS_num_processes * FLAGS_num_threads;
  c->session_num_vec.resize(num_sessions);
  std::fill(c->session_num_vec.begin(), c->session_num_vec.end(), -1);

  // Initiate connection for sessions
  fprintf(stderr,
          "large_rpc_tput: Process %zu, thread %zu: Creating %zu sessions. "
          "Profile = 'random'.\n",
          FLAGS_process_id, c->thread_id, num_sessions);
  for (size_t p_i = 0; p_i < FLAGS_num_processes; p_i++) {
    const std::string rem_uri = erpc::get_uri_for_process(p_i);

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      size_t session_idx = (p_i * FLAGS_num_threads) + t_i;
      if (session_idx == c->self_session_idx) continue;  // No session to self

      c->session_num_vec[session_idx] =
          c->rpc->create_session(rem_uri, static_cast<uint8_t>(t_i));

      erpc::rt_assert(c->session_num_vec[session_idx] >= 0,
                      "Failed to create session.");
    }
  }

  while (c->num_sm_resps != FLAGS_num_processes * FLAGS_num_threads - 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

#endif
