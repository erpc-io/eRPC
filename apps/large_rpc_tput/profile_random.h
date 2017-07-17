#ifndef PROFILE_RANDOM_H
#define PROFILE_RANDOM_H

#include "large_rpc_tput.h"

// The session index selection function for the "random" profile
size_t get_session_index_func_random(AppContext *c) {
  assert(c != nullptr);
  size_t session_vec_size = c->session_num_vec.size();

  // We don't need Lemire's trick bc messages are large
  size_t rand_session_index = c->fastrand.next_u32() % session_vec_size;
  while (rand_session_index == c->self_session_index) {
    rand_session_index = c->fastrand.next_u32() % session_vec_size;
  }

  return rand_session_index;
}

void connect_sessions_func_random(AppContext *c) {
  // Allocate per-session info
  size_t num_sessions = FLAGS_num_machines * FLAGS_num_threads;
  c->session_num_vec.resize(num_sessions);
  std::fill(c->session_num_vec.begin(), c->session_num_vec.end(), -1);

  c->stat_resp_rx_bytes.resize(num_sessions);
  std::fill(c->stat_resp_rx_bytes.begin(), c->stat_resp_rx_bytes.end(), 0);

  // Initiate connection for sessions
  fprintf(stderr, "large_rpc_tput: Thread %zu: Creating %zu sessions.\n",
          c->thread_id, num_sessions);
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    std::string hostname = get_hostname_for_machine(m_i);

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      size_t session_index = (m_i * FLAGS_num_threads) + t_i;
      // Do not create a session to self
      if (session_index == c->self_session_index) continue;

      c->session_num_vec[session_index] = c->rpc->create_session(
          hostname, static_cast<uint8_t>(t_i), kAppPhyPort);
      assert(c->session_num_vec[session_index] >= 0);
    }
  }

  while (c->num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

#endif
