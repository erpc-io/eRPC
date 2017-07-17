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

#endif
