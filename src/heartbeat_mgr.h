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
  static constexpr bool kVerbose = true;
  enum class EventType : bool {
    kSend,  // Send a heartbeat to the remote URI
    kCheck  // Check that we have received a heartbeat from the remote URI
  };

  /// Heartbeat info in the priority queue
  class Event {
   public:
    EventType type_;
    std::string
        rem_uri_;   // The remote process for receiving/sending heartbeats
    uint64_t tsc_;  // The time at which this event is triggered

    Event(EventType type, const std::string &rem_uri, uint64_t tsc)
        : type_(type), rem_uri_(rem_uri), tsc_(tsc) {}
  };

  struct event_comparator {
    bool operator()(const Event &p1, const Event &p2) {
      return p1.tsc_ > p2.tsc_;
    }
  };

 public:
  HeartbeatMgr(std::string hostname, uint16_t sm_udp_port, double freq_ghz,
               size_t machine_failure_timeout_ms)
      : hostname_(hostname),
        sm_udp_port_(sm_udp_port),
        freq_ghz_(freq_ghz),
        creation_tsc_(rdtsc()),
        failure_timeout_tsc_(
            ms_to_cycles(machine_failure_timeout_ms, freq_ghz)),
        hb_send_delta_tsc_(failure_timeout_tsc_ / 10),
        hb_check_delta_tsc_(failure_timeout_tsc_ / 2) {}

  /// Add a remote URI to the tracking set
  void unlocked_add_remote(const std::string &remote_uri) {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    if (kVerbose) {
      printf("heartbeat_mgr (%.0f us): Starting tracking URI %s\n",
             us_since_creation(rdtsc()), remote_uri.c_str());
    }

    size_t cur_tsc = rdtsc();

    map_last_hb_rx_.emplace(remote_uri, cur_tsc);
    schedule_hb_send(remote_uri);
    schedule_hb_check(remote_uri);
  }

  /// Receive a heartbeat
  void unlocked_receive_hb(const SmPkt &sm_pkt) {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);

    if (map_last_hb_rx_.count(sm_pkt.client_.uri()) == 0) {
      if (kVerbose) {
        printf("heartbeat_mgr (%.0f us): Ignoring heartbeat from URI %s\n",
               us_since_creation(rdtsc()), sm_pkt.client_.uri().c_str());
      }
      return;
    }

    if (kVerbose) {
      printf("heartbeat_mgr (%.0f us): Receiving heartbeat from URI %s\n",
             us_since_creation(rdtsc()), sm_pkt.client_.uri().c_str());
    }
    map_last_hb_rx_.emplace(sm_pkt.client_.uri(), rdtsc());
  }

  /**
   * @brief The main heartbeat work: Send heartbeats, and check expired timers
   *
   * @param failed_uris The list of failed remote URIs to fill-in
   */
  void do_one(std::vector<std::string> &failed_uris) {
    while (true) {
      if (hb_event_pqueue_.empty()) break;

      const auto next_ev = hb_event_pqueue_.top();  // Copy-out

      // Stop processing the event queue when the top event is in the future
      if (in_future(next_ev.tsc_)) {
        if (kVerbose) {
          printf("heartbeat_mgr (%.0f us): Event %s is in the future\n",
                 us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
        }
        break;
      }

      if (map_last_hb_rx_.count(next_ev.rem_uri_) == 0) {
        if (kVerbose) {
          printf(
              "heartbeat_mgr (%.0f us): Remote URI for event %s is not "
              "in tracking set. Ignoring.\n",
              us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
        }
        break;
      }

      if (kVerbose) {
        printf("heartbeat_mgr (%.0f us): Handling event %s\n",
               us_since_creation(rdtsc()), ev_to_string(next_ev).c_str());
      }

      hb_event_pqueue_.pop();  // Consume the event

      switch (next_ev.type_) {
        case EventType::kSend: {
          SmPkt heartbeat =
              make_heartbeat(hostname_, sm_udp_port_, next_ev.rem_uri_);
          hb_udp_client_.send(heartbeat.server_.hostname_,
                              heartbeat.server_.sm_udp_port_, heartbeat);

          schedule_hb_send(next_ev.rem_uri_);
          break;
        }

        case EventType::kCheck: {
          size_t last_ping_rx = map_last_hb_rx_[next_ev.rem_uri_];
          if (rdtsc() - last_ping_rx > failure_timeout_tsc_) {
            failed_uris.push_back(next_ev.rem_uri_);

            if (kVerbose) {
              printf("heartbeat_mgr (%.0f us): Remote URI %s failed\n",
                     us_since_creation(rdtsc()), next_ev.rem_uri_.c_str());
            }
            map_last_hb_rx_.erase(map_last_hb_rx_.find(next_ev.rem_uri_));
          } else {
            schedule_hb_check(next_ev.rem_uri_.c_str());
          }
          break;
        }
      }
    }
  }

 private:
  // Create a heartbeat packet send by the local URI to the remote URI.
  //
  // A heartbeat packet is a session management packet where most fields are
  // invalid. The local sender acts as the SmPkt's client.
  static SmPkt make_heartbeat(const std::string &local_hostname,
                              uint16_t local_sm_udp_port,
                              const std::string &remote_uri) {
    SmPkt sm_hb;
    sm_hb.pkt_type_ = SmPktType::kPingReq;
    sm_hb.err_type_ = SmErrType::kNoError;
    sm_hb.uniq_token_ = 0;

    std::string remote_hostname;
    uint16_t remote_sm_udp_port;
    split_uri(remote_uri, remote_hostname, remote_sm_udp_port);

    // In sm_hb's session endpoints, the transport type, Rpc ID, and session
    // number are already invalid.
    strcpy(sm_hb.client_.hostname_, local_hostname.c_str());
    sm_hb.client_.sm_udp_port_ = local_sm_udp_port;

    strcpy(sm_hb.server_.hostname_, remote_hostname.c_str());
    sm_hb.server_.sm_udp_port_ = remote_sm_udp_port;

    return sm_hb;
  }

  /// Return the microseconds between tsc and this manager's creation time
  double us_since_creation(size_t tsc) const {
    return to_usec(tsc - creation_tsc_, freq_ghz_);
  }

  /// Return a string representation of a ping event. This is outside the
  /// Event class because we need creation_tsc.
  std::string ev_to_string(const Event &e) const {
    std::ostringstream ret;
    ret << "[Type: " << (e.type_ == EventType::kSend ? "send" : "check")
        << ", URI " << e.rem_uri_ << ", time "
        << static_cast<size_t>(us_since_creation(e.tsc_)) << " us]";
    return ret.str();
  }

  /// Return true iff a timestamp is in the future
  static bool in_future(size_t tsc) { return tsc > rdtsc(); }

  void schedule_hb_send(const std::string &rem_uri) {
    Event e(EventType::kSend, rem_uri, rdtsc() + hb_send_delta_tsc_);
    if (kVerbose) {
      printf("heartbeat_mgr (%.0f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    hb_event_pqueue_.push(e);
  }

  void schedule_hb_check(const std::string &rem_uri) {
    Event e(EventType::kCheck, rem_uri, rdtsc() + hb_check_delta_tsc_);
    if (kVerbose) {
      printf("heartbeat_mgr (%.0f us): Scheduling event %s\n",
             us_since_creation(rdtsc()), ev_to_string(e).c_str());
    }
    hb_event_pqueue_.push(e);
  }

  const std::string hostname_;  /// This process's local hostname
  const uint16_t sm_udp_port_;  /// This process's management UDP port

  const double freq_ghz_;             /// TSC frequency
  const size_t creation_tsc_;         /// Time at which this manager was created
  const size_t failure_timeout_tsc_;  /// Machine failure timeout in TSC cycles

  /// We send heartbeats every every hb_send_delta_tsc cycles. This duration is
  /// around a tenth of the failure timeout.
  const size_t hb_send_delta_tsc_;

  /// We check heartbeats every every hb_check_delta_tsc cycles. This duration
  /// is around half of the failure timeout #nyquist.
  const size_t hb_check_delta_tsc_;

  std::priority_queue<Event, std::vector<Event>, event_comparator>
      hb_event_pqueue_;

  // This map servers two purposes:
  //
  // 1. Its value for a remote URI is the timestamp when we last received a
  //    heartbeat from the remote URI.
  // 2. The set of remote URIs in the map is our remote tracking set. Since we
  //    cannot delete efficiently from the event priority queue, events for
  //    remote URIs not in this map are ignored when they are dequeued.
  std::unordered_map<std::string, uint64_t> map_last_hb_rx_;

  UDPClient<SmPkt> hb_udp_client_;

  std::mutex heartbeat_mutex_;  // Protects this heartbeat manager
};
}  // namespace erpc
