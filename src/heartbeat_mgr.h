#pragma once

#include <queue>
#include <sstream>
#include <unordered_map>
#include "common.h"
#include "rpc_constants.h"
#include "sm_types.h"
#include "util/autorun_helpers.h"
#include "util/timer.h"
#include "util/udp_client.h"

namespace erpc {

/**
 * @brief A thread-safe heartbeat manager
 *
 * This class has two main tasks: First, it sends heartbeat messages to remote
 * processes at fixed intervals. Second, it checks for timeouts, also at fixed
 * intervals. These tasks are scheduled using a time-based priority queue.
 *
 * For efficiency, if a process has multiple sessions to a remote process,
 * only one instance of the remote URI is tracked.
 *
 * This heartbeat manager is designed to keep the CPU use of eRPC's management
 * thread close to zero in the steady state. An earlier version of eRPC's
 * timeout detection used a reliable UDP library called ENet, which had
 * non-negligible CPU use.
 */
class HeartbeatMgr {
 private:
  static constexpr bool kVerbose = false;
  enum class EventType : bool {
    kSend,  // Send a heartbeat to the remote URI
    kCheck  // Check that we have received a heartbeat from the remote URI
  };

  /// Heartbeat info in the priority queue
  class Event {
   public:
    EventType type;
    std::string rem_uri;  // The remote process for receiving/sending heartbeats
    uint64_t tsc;         // The time at which this event is triggered

    Event(EventType type, const std::string &rem_uri, uint64_t tsc)
        : type(type), rem_uri(rem_uri), tsc(tsc) {}
  };

  struct EventComparator {
    bool operator()(const Event &p1, const Event &p2) {
      return p1.tsc > p2.tsc;
    }
  };

 public:
  HeartbeatMgr(std::string hostname, uint16_t sm_udp_port, double freq_ghz,
               size_t machine_failure_timeout_ms)
      : hostname(hostname),
        sm_udp_port(sm_udp_port),
        freq_ghz(freq_ghz),
        creation_tsc(rdtsc()),
        failure_timeout_tsc(ms_to_cycles(machine_failure_timeout_ms, freq_ghz)),
        hb_send_delta_tsc(failure_timeout_tsc / 10),
        hb_check_delta_tsc(failure_timeout_tsc / 2) {}

  /// Add a remote URI to the tracking set
  void unlocked_add_remote(const std::string &remote_uri) {
    std::lock_guard<std::mutex> lock(heartbeat_mutex);
    size_t cur_tsc = rdtsc();

    map_last_hb_rx.emplace(remote_uri, cur_tsc);
    schedule_hb_send(remote_uri);
    schedule_hb_check(remote_uri);
  }

  /// Receive any heartbeat
  void unlocked_receive_hb(const SmPkt &sm_pkt) {
    std::lock_guard<std::mutex> lock(heartbeat_mutex);

    if (map_last_hb_rx.count(sm_pkt.client.uri()) == 0) return;
    map_last_hb_rx.emplace(sm_pkt.client.uri(), rdtsc());
  }

  /**
   * @brief The main heartbeat work: Send heartbeats, and check expired timers
   *
   * @param failed_uris The list of failed remote URIs to fill-in
   */
  void do_one(std::vector<std::string> &failed_uris) {
    while (true) {
      if (hb_event_pqueue.empty()) break;

      const auto next_ev = hb_event_pqueue.top();  // Copy-out

      // Stop processing the event queue when the top event is in the future
      if (in_future(next_ev.tsc)) {
        if (kVerbose) {
          printf("heartbeat_mgr (%.1f us): Event %s is in the future\n",
                 us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
        }
        break;
      }

      if (map_last_hb_rx.count(next_ev.rem_uri) == 0) {
        if (kVerbose) {
          printf(
              "heartbeat_mgr (%.1f us): Remote URI for event %s is not "
              "in tracking set. Ignoring.\n",
              us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
        }
        break;
      }

      if (kVerbose) {
        printf("heartbeat_mgr (%.1f us): Handling event %s\n",
               us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
      }

      hb_event_pqueue.pop();  // Consume the event

      switch (next_ev.type) {
        case EventType::kSend: {
          SmPkt heartbeat = make_heartbeat(next_ev.rem_uri);
          hb_udp_client.send(heartbeat.server.hostname,
                             heartbeat.server.sm_udp_port, heartbeat);

          schedule_hb_send(next_ev.rem_uri);
          break;
        }

        case EventType::kCheck: {
          size_t last_ping_rx = map_last_hb_rx[next_ev.rem_uri];
          if (rdtsc() - last_ping_rx > failure_timeout_tsc) {
            failed_uris.push_back(next_ev.rem_uri);

            // Stop tracking rem_uri
            if (kVerbose) {
              printf("heartbeat_mgr (%.1f us): Stopping tracking %s\n",
                     us_since_creation(rdtsc()), next_ev.rem_uri.c_str());
            }
            map_last_hb_rx.erase(map_last_hb_rx.find(next_ev.rem_uri));
          } else {
            schedule_hb_check(next_ev.rem_uri.c_str());
          }
          break;
        }
      }
    }
  }

 private:
  // A heartbeat packet is a management packet where most fields are invalid.
  // The SmPkt's client is the sender.
  SmPkt make_heartbeat(const std::string &remote_uri) {
    SmPkt req;
    req.pkt_type = SmPktType::kPingReq;
    req.err_type = SmErrType::kNoError;
    req.uniq_token = 0;

    std::string remote_hostname;
    uint16_t remote_sm_udp_port;
    split_uri(remote_uri, remote_hostname, remote_sm_udp_port);

    // In req's session endpoints, the transport type, Rpc ID, and session
    // number are already invalid.
    strcpy(req.client.hostname, hostname.c_str());
    req.client.sm_udp_port = sm_udp_port;

    strcpy(req.server.hostname, remote_hostname.c_str());
    req.server.sm_udp_port = remote_sm_udp_port;

    return req;
  }

  /// Return the microseconds between tsc and this manager's creation time
  double us_since_creation(size_t tsc) const {
    return to_usec(tsc - creation_tsc, freq_ghz);
  }

  /// Return a string representation of a ping event. This is outside the
  /// Event class because we need creation_tsc.
  std::string ev_to_string(const Event &e) const {
    std::ostringstream ret;
    ret << "[Type: " << (e.type == EventType::kSend ? "send" : "check")
        << ", URI " << e.rem_uri << ", time " << us_since_creation(e.tsc)
        << " us]";
    return ret.str();
  }

  /// Return true iff a timestamp is in the future
  static bool in_future(size_t tsc) { return tsc > rdtsc(); }

  void schedule_hb_send(const std::string &rem_uri) {
    Event e(EventType::kSend, rem_uri, rdtsc() + hb_send_delta_tsc);
    if (kVerbose) {
      printf("heartbeat_mgr (%.1f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    hb_event_pqueue.push(e);
  }

  void schedule_hb_check(const std::string &rem_uri) {
    Event e(EventType::kCheck, rem_uri, rdtsc() + hb_check_delta_tsc);
    if (kVerbose) {
      printf("heartbeat_mgr (%.1f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    hb_event_pqueue.push(e);
  }

  const std::string hostname;  /// This process's local hostname
  const uint16_t sm_udp_port;  /// This process's management UDP port

  const double freq_ghz;             /// TSC frequency
  const size_t creation_tsc;         /// Time at which this manager was created
  const size_t failure_timeout_tsc;  /// Machine failure timeout in TSC cycles

  /// We send heartbeats every every hb_send_delta_tsc cycles. This duration is
  /// around a tenth of the failure timeout.
  const size_t hb_send_delta_tsc;

  /// We check heartbeats every every hb_check_delta_tsc cycles. This duration
  /// is around half of the failure timeout #nyquist.
  const size_t hb_check_delta_tsc;

  std::priority_queue<Event, std::vector<Event>, EventComparator>
      hb_event_pqueue;

  // This map servers two purposes:
  //
  // 1. Its value for a remote URI is the timestamp when we last received a
  //    heartbeat from the remote URI.
  // 2. The set of remote URIs in the map is our remote tracking set. Since we
  //    cannot delete efficiently from the event priority queue, events for
  //    remote URIs not in this map are ignored when they are dequeued.
  std::unordered_map<std::string, uint64_t> map_last_hb_rx;

  UDPClient<SmPkt> hb_udp_client;

  std::mutex heartbeat_mutex;  // Protects this heartbeat manager
};
}
