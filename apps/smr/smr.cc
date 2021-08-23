#include "smr.h"
#include <util/autorun_helpers.h>
#include "client.h"
#include "server.h"
#include "util/numautils.h"

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  signal(SIGINT, ctrl_c_handler);
  erpc::rt_assert(FLAGS_num_raft_servers > 0 &&
                  FLAGS_num_raft_servers % 2 == 1);

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);

  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    node_id_to_name_map[get_raft_node_id_for_process(i)] =
        erpc::trim_hostname(erpc::get_uri_for_process(i));
  }

  if (kColocateClientWithLastServer) {
    AppContext server_context;
    server_context.conn_vec.resize(FLAGS_num_raft_servers);
    for (auto &peer_conn : server_context.conn_vec)
      peer_conn.c = &server_context;

    AppContext client_context;
    client_context.conn_vec.resize(FLAGS_num_raft_servers);
    for (auto &peer_conn : client_context.conn_vec)
      peer_conn.c = &client_context;

    // Run two threads on the last server because I have only three machines
    if (FLAGS_process_id == FLAGS_num_raft_servers - 1) {
      auto server_thread = std::thread(server_func, &nexus, &server_context);
      auto client_thread = std::thread(client_func, &nexus, &client_context);

      erpc::bind_to_core(server_thread, FLAGS_numa_node, 0);
      erpc::bind_to_core(client_thread, FLAGS_numa_node, 1);
      server_thread.join();
      client_thread.join();
    } else {
      auto thread = std::thread(server_func, &nexus, &server_context);
      erpc::bind_to_core(thread, FLAGS_numa_node, 0);
      thread.join();
    }
  } else {
    AppContext c;
    c.conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers
    for (auto &peer_conn : c.conn_vec) peer_conn.c = &c;

    auto thread =
        std::thread(is_raft_server() ? server_func : client_func, &nexus, &c);
    erpc::bind_to_core(thread, FLAGS_numa_node, 2);
    thread.join();
  }
}
