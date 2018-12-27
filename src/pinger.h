#pragma once

#include <queue>
#include <unordered_map>
#include "common.h"
#include "rpc_constants.h"
#include "sm_types.h"
#include "util/timer.h"
#include "util/udp_client.h"

namespace erpc {

/// A thread-safe class for managing keepalive pings
class Pinger {
 public:
  enum class PingEventType : bool { kSend, kCheck };

  Pinger(double freq_ghz)
      : failure_timeout_tsc(ms_to_cycles(kServerFailureTimeoutMs, freq_ghz)),
        ping_send_delta_tsc(failure_timeout_tsc / 10),
        ping_check_delta_tsc(failure_timeout_tsc / 2) {}

  /// Information saved by the SM thread about a management ping
  class PingEvent {
   public:
    PingEventType type;
    std::string hostname;  // The remote node that the ping was sent to
    uint64_t tsc;          // The time at which this event is triggered

    PingEvent(PingEventType type, const char *hostname, uint64_t tsc)
        : type(type), hostname(hostname), tsc(tsc) {}
  };

  struct PingEventComparator {
    bool operator()(const PingEvent &p1, const PingEvent &p2) {
      return p1.tsc < p2.tsc;
    }
  };

  /// Add a remote server to the tracking set
  void unlocked_add_remote_server(const char *server_hostname) {
    std::lock_guard<std::mutex> lock(pinger_mutex);
    size_t cur_tsc = rdtsc();
    map_last_ping_rx.emplace(server_hostname, cur_tsc);

    enqueue_ping_send(server_hostname);
    enqueue_ping_check(server_hostname);
  }

  /// Add a remote client to the tracking set
  void unlocked_add_remote_client(const char *client_hostname) {
    std::lock_guard<std::mutex> lock(pinger_mutex);
    size_t cur_tsc = rdtsc();

    map_last_ping_rx.emplace(client_hostname, cur_tsc);

    enqueue_ping_check(client_hostname);
  }

  /// Receive a ping request from a remote client or server
  void unlocked_receive_ping_req_or_resp(const char *remote_hostname) {
    std::lock_guard<std::mutex> lock(pinger_mutex);
    map_last_ping_rx.emplace(remote_hostname, rdtsc());
  }

 private:
  /// The main ping work: send pings, and check expired timers
  void do_one() {
    while (true) {
      if (ping_event_queue.empty()) break;

      const auto next_ev = ping_event_queue.top();  // Copy-out
      if (in_future(next_ev.tsc)) break;

      ping_event_queue.pop();  // Consume the event

      switch (next_ev.type) {
        case PingEventType::kSend: {
          // XXX: Send a ping to next_ev.hostname
          enqueue_ping_send(next_ev.hostname.c_str());
          break;
        }

        case PingEventType::kCheck: {
          assert(map_last_ping_rx.count(next_ev.hostname) == 1);
          size_t last_ping_rx = map_last_ping_rx[next_ev.hostname];
          if (rdtsc() - last_ping_rx > failure_timeout_tsc) {
            // XXX: next_ev.hostname has failed
          }
          break;
        }
      }
    }
  }

  /// Return true iff a timestamp is in the future
  static bool in_future(size_t tsc) { return tsc > rdtsc(); }

  void enqueue_ping_send(const char *hostname) {
    ping_event_queue.emplace(PingEventType::kSend, hostname,
                             rdtsc() + ping_send_delta_tsc);
  }

  void enqueue_ping_check(const char *hostname) {
    ping_event_queue.emplace(PingEventType::kCheck, hostname,
                             rdtsc() + ping_check_delta_tsc);
  }

  const size_t failure_timeout_tsc;  // Machine failure timeout in TSC cycles

  // We send pings every every ping_send_delta_tsc cycles. This duration is
  // around a tenth of the failure timeout.
  const size_t ping_send_delta_tsc;

  // We check pings every every ping_check_delta_tsc cycles. This duration is
  // around half of the failure timeout. XXX: Explain.
  const size_t ping_check_delta_tsc;

  std::priority_queue<PingEvent, std::vector<PingEvent>, PingEventComparator>
      ping_event_queue;
  std::unordered_map<std::string, uint64_t> map_last_ping_rx;

  UDPClient<SmPkt> ping_udp_client;

  std::mutex pinger_mutex;  // Protects this pinger
};
}
