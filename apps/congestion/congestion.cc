/**
 * @file congestion.cc
 *
 * @brief Benchmark to evaluate congestion control
 *
 * With N (> 2) processes, session connectivity is as follows:
 *  o Process 0 acts as the incast receiver
 *  o A subset of "incast" threads at processes {1, ..., N - 1} send incast
 *    traffic to threads on process 0. Each such thread creates one session to
 *    process 0.
 *  o The remaining subset of "regular" threads at processes {2, ..., N - 1}
 *    exchange non-incast traffic. Each regular thread creates a session to
 *    every regular thread.
 *
 * Process 0 runs incast_threads_zero threads. Other processes run
 * (incast_threads_other + regular_threads_other) threads.
 */

#include "congestion.h"
#include <signal.h>
#include <cstring>
#include "incast_impl.h"
#include "regular_impl.h"
#include "util/autorun_helpers.h"

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  erpc::rt_assert(FLAGS_num_processes >= 3, "Too few processes");
  erpc::rt_assert(FLAGS_process_id < FLAGS_num_processes, "Invalid process ID");
  erpc::rt_assert(FLAGS_regular_concurrency <= kAppMaxConcurrency,
                  "Invalid concurrency");

  // Supporting zero threads is troublesome (e.g., who prints the stats?)
  erpc::rt_assert(FLAGS_incast_threads_zero >= 1, "Need > 0 incast threads");
  erpc::rt_assert(FLAGS_incast_threads_other >= 1, "Need > 0 incast threads");
  erpc::rt_assert(FLAGS_regular_threads_other >= 1, "Need > 0 regular threads");
  erpc::rt_assert(FLAGS_regular_latency_divisor >= 1.0, "Invalid latency div");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqTypeIncast, req_handler_incast);
  nexus.register_req_func(kAppReqTypeRegular, req_handler_regular);

  size_t num_threads = FLAGS_process_id == 0 ? FLAGS_incast_threads_zero
                                             : FLAGS_incast_threads_other +
                                                   FLAGS_regular_threads_other;
  std::vector<std::thread> threads(num_threads);
  auto *app_stats = new app_stats_t[num_threads];

  for (size_t i = 0; i < num_threads; i++) {
    if (FLAGS_process_id == 0) {
      // Incast receiver
      threads[i] = std::thread(thread_func_incast_zero, i, &nexus);
    } else {
      if (i < FLAGS_incast_threads_other) {
        // Incast sender
        threads[i] =
            std::thread(thread_func_incast_other, i, app_stats, &nexus);
      } else {
        // Non-incast, "regular" threads
        threads[i] = std::thread(thread_func_regular, i, app_stats, &nexus);
      }
    }
    erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
  }

  for (auto &thread : threads) thread.join();
  delete[] app_stats;
}
