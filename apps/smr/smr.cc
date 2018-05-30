#include "smr.h"
#include <util/autorun_helpers.h>
#include "appendentries.h"
#include "callbacks.h"
#include "client.h"
#include "requestvote.h"
#include "server.h"
#include "util/numautils.h"

void init_raft_node_id_to_name_map() {
  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    std::string uri = erpc::get_uri_for_process(i);
    int raft_node_id = get_raft_node_id_from_uri(uri);
    node_id_to_name_map[raft_node_id] = erpc::trim_hostname(uri);
  }
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  signal(SIGINT, ctrl_c_handler);
  erpc::rt_assert(FLAGS_num_raft_servers > 0 &&
                  FLAGS_num_raft_servers % 2 == 1);

  AppContext c;
  c.conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers
  for (auto &peer_conn : c.conn_vec) peer_conn.c = &c;

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);

  // Both server and client need this map, so init it before launching client
  init_raft_node_id_to_name_map();

  auto thread =
      std::thread(is_raft_server() ? server_func : client_func, 0, &nexus, &c);
  erpc::bind_to_core(thread, 0, 0);
  thread.join();
}
