#ifndef PROFILE_VICTIM_H
#define PROFILE_VICTIM_H

#include "large_rpc_tput.h"

void connect_sessions_func_victim(AppContext *c) {
  if (FLAGS_process_id == 0) return;
  if (FLAGS_process_id == FLAGS_num_processes - 1) return;

  size_t server_process_id;
  if (FLAGS_process_id != FLAGS_num_processes - 2) {
    server_process_id = 0;
  } else {
    server_process_id =
        c->thread_id == FLAGS_num_threads - 1 ? FLAGS_num_processes - 1 : 0;
  }

  c->session_num_vec.resize(1);

  printf("large_rpc_tput: Thread %zu: Creating 1 session. Profile 'victim'.\n",
         c->thread_id);

  c->session_num_vec[0] =
      c->rpc->create_session(erpc::get_uri_for_process(server_process_id),
                             static_cast<uint8_t>(c->thread_id));
  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session failed.");

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  // If throttling is enabled, flows to the incast victim are throttled
  if (server_process_id == 0 && FLAGS_throttle == 1) {
    erpc::Timely *timely_0 = c->rpc->get_timely(c->session_num_vec[0]);
    double num_incast_flows =
        ((FLAGS_num_processes - 2) * FLAGS_num_threads) - 1;
    double fair_share = erpc::kBandwidth / num_incast_flows;
    timely_0->rate = fair_share * FLAGS_throttle_fraction;
  }
}

#endif
