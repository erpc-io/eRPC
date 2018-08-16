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

  AppContext c;
  c.conn_vec.resize(FLAGS_num_raft_servers);  // Both clients and servers
  for (auto &peer_conn : c.conn_vec) peer_conn.c = &c;

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);

  for (size_t i = 0; i < FLAGS_num_raft_servers; i++) {
    node_id_to_name_map[get_raft_node_id_for_process(i)] =
        erpc::trim_hostname(erpc::get_uri_for_process(i));
  }

  auto thread =
      std::thread(is_raft_server() ? server_func : client_func, 0, &nexus, &c);
  erpc::bind_to_core(thread, 0, 0);
  thread.join();
}
