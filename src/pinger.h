#pragma once

#include <queue>
#include <sstream>
#include <unordered_map>
#include "common.h"
#include "rpc_constants.h"
#include "sm_types.h"
#include "util/timer.h"
#include "util/udp_client.h"

namespace erpc {

/**
 * @brief A thread-safe keepalive pinger
 *
 * This class has two main tasks: For RPC clients, it sends ping requests to
 * servers at fixed intervals. For both RPC clients and servers, it checks for
 * timeouts, also at fixed intervals. These tasks are scheduled using a
 * time-based priority queue.
 *
 * For efficiency, if a machine creates multiple sessions to a remote machine,
 * only one instance of the remote URI is tracked by the pinger.
 *
 * This pinger is designed to keep the CPU use of eRPC's management thread close
 * to zero in the steady state. An earlier version of eRPC's timeout detection
 * used a reliable UDP library called ENet, which had non-negligible CPU use.
 */
class Pinger {
 private:
  static constexpr bool kVerbose = false;
  enum class PingEventType : bool { kSend, kCheck };

  /// Management ping info in the priority queue
  class PingEvent {
   public:
    PingEventType type;
    std::string rem_uri;  // The remote process for receiving/sending pings
    uint64_t tsc;         // The time at which this event is triggered

    PingEvent(PingEventType type, const std::string &rem_uri, uint64_t tsc)
        : type(type), rem_uri(rem_uri), tsc(tsc) {}
  };

  struct PingEventComparator {
    bool operator()(const PingEvent &p1, const PingEvent &p2) {
      return p1.tsc > p2.tsc;
    }
  };

 public:
  Pinger(double freq_ghz, size_t machine_failure_timeout_ms)
      : freq_ghz(freq_ghz),
        creation_tsc(rdtsc()),
        failure_timeout_tsc(ms_to_cycles(machine_failure_timeout_ms, freq_ghz)),
        ping_send_delta_tsc(failure_timeout_tsc / 10),
        ping_check_delta_tsc(failure_timeout_tsc / 2) {}

  /// Add a remote server to the tracking set
  void unlocked_add_remote_server(const std::string &rem_uri) {
    std::lock_guard<std::mutex> lock(pinger_mutex);
    size_t cur_tsc = rdtsc();
    map_last_ping_rx.emplace(rem_uri, cur_tsc);

    schedule_ping_send(rem_uri);
    schedule_ping_check(rem_uri);
  }

  /// Add a remote client to the tracking set
  void unlocked_add_remote_client(const std::string &client_uri) {
    std::lock_guard<std::mutex> lock(pinger_mutex);
    size_t cur_tsc = rdtsc();

    map_last_ping_rx.emplace(client_uri, cur_tsc);
    schedule_ping_check(client_uri);
  }

  /// Receive any ping packet
  void unlocked_receive_ping_req_or_resp(const SmPkt &sm_pkt) {
    std::lock_guard<std::mutex> lock(pinger_mutex);

    const auto rem_uri =
        sm_pkt.is_req() ? sm_pkt.client.uri() : sm_pkt.server.uri();

    if (map_last_ping_rx.count(rem_uri) == 0) return;
    map_last_ping_rx.emplace(rem_uri, rdtsc());
  }

  /**
   * @brief The main pinging work: Send keepalive pings, and check expired
   * timers
   *
   * @param failed_uris The list of failed remote URIs to fill-in
   */
  void do_one(std::vector<std::string> &failed_uris) {
    while (true) {
      if (ping_event_queue.empty()) break;

      const auto next_ev = ping_event_queue.top();  // Copy-out
      if (in_future(next_ev.tsc)) {
        if (kVerbose) {
          printf("pinger (%.3f us): Event %s is in the future\n",
                 us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
        }
        break;
      }

      if (kVerbose) {
        printf("pinger (%.3f us): Handling event %s\n",
               us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
      }

      ping_event_queue.pop();  // Consume the event

      switch (next_ev.type) {
        case PingEventType::kSend: {
          schedule_ping_send(next_ev.rem_uri);
          break;
        }

        case PingEventType::kCheck: {
          assert(map_last_ping_rx.count(next_ev.rem_uri) == 1);
          size_t last_ping_rx = map_last_ping_rx[next_ev.rem_uri];
          if (rdtsc() - last_ping_rx > failure_timeout_tsc) {
            failed_uris.push_back(next_ev.rem_uri);
          } else {
            schedule_ping_check(next_ev.rem_uri.c_str());
          }
          break;
        }
      }
    }
  }

 private:
  /// Return the microseconds between tsc and this pinger's creation time
  double us_since_creation(size_t tsc) const {
    return to_usec(tsc - creation_tsc, freq_ghz);
  }

  /// Return a string representation of a ping event. This is outside the
  /// PingEvent class because we need creation_tsc.
  std::string ev_to_string(const PingEvent &e) const {
    std::ostringstream ret;
    ret << "[Type: " << (e.type == PingEventType::kSend ? "send" : "check")
        << ", URI " << e.rem_uri << ", time " << us_since_creation(e.tsc)
        << " us]";
    return ret.str();
  }

  /// Return true iff a timestamp is in the future
  static bool in_future(size_t tsc) { return tsc > rdtsc(); }

  void schedule_ping_send(const std::string &rem_uri) {
    PingEvent e(PingEventType::kSend, rem_uri, rdtsc() + ping_send_delta_tsc);
    if (kVerbose) {
      printf("pinger (%.3f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    ping_event_queue.push(e);
  }

  void schedule_ping_check(const std::string &rem_uri) {
    PingEvent e(PingEventType::kCheck, rem_uri, rdtsc() + ping_check_delta_tsc);
    if (kVerbose) {
      printf("pinger (%.3f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    ping_event_queue.push(e);
  }

  const double freq_ghz;             // TSC frequency
  const size_t creation_tsc;         // Time at which this pinger was created
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
