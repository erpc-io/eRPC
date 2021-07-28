#include <gtest/gtest.h>
#include <algorithm>

#define private public
#include "heartbeat_mgr.h"

using namespace erpc;

static constexpr double kTestFreqGhz = 2.5;
static constexpr double kTestMachineFailureTimeoutMs = 50;
static constexpr const char *kTestLocalHostname = "127.0.0.1";
static constexpr uint16_t kTestLocalSmUdpPort = 31850;
static constexpr const char *kTestLocalUri = "127.0.0.1:31850";

/// Return true iff vec contains s
static bool str_vec_contains(const std::vector<std::string> &vec,
                             const std::string &s) {
  return std::find(vec.begin(), vec.end(), s) != vec.end();
}

TEST(HeartbeatMgrTest, PriorityQueueOrderTest) {
  std::priority_queue<HeartbeatMgr::Event, std::vector<HeartbeatMgr::Event>,
                      HeartbeatMgr::event_comparator>
      hb_event_pqueue;
  hb_event_pqueue.emplace(HeartbeatMgr::EventType::kSend, "hostname_1", 1);
  hb_event_pqueue.emplace(HeartbeatMgr::EventType::kCheck, "hostname_2", 3);
  hb_event_pqueue.emplace(HeartbeatMgr::EventType::kSend, "hostname_3", 2);

  assert(hb_event_pqueue.top().tsc_ == 1);
  hb_event_pqueue.pop();

  assert(hb_event_pqueue.top().tsc_ == 2);
  hb_event_pqueue.pop();

  assert(hb_event_pqueue.top().tsc_ == 3);
  hb_event_pqueue.pop();
}

TEST(HeartbeatMgrTest, URISplitTest) {
  std::string hostname;
  uint16_t udp_port;

  std::string uri = "server1:12345";
  split_uri(uri, hostname, udp_port);
  assert(hostname == "server1" && udp_port == 12345);

  uri = "akalianode-5.RDMA.ron-PG0.wisc.cloudlab.us:31850";
  split_uri(uri, hostname, udp_port);
  assert(hostname == "akalianode-5.RDMA.ron-PG0.wisc.cloudlab.us" &&
         udp_port == 31850);

  uri = "192.168.18.2:1";
  split_uri(uri, hostname, udp_port);
  assert(hostname == "192.168.18.2" && udp_port == 1);
}

TEST(HeartbeatMgrTest, Basic) {
  HeartbeatMgr heartbeat_mgr(kTestLocalHostname, kTestLocalSmUdpPort,
                             kTestFreqGhz, kTestMachineFailureTimeoutMs);
  std::vector<std::string> failed_uris;

  heartbeat_mgr.hb_udp_client_.enable_recording();
  std::vector<SmPkt> &sent_vec = heartbeat_mgr.hb_udp_client_.sent_vec_;

  // The manager will sent actual UDP packets, so we need valid addresses
  heartbeat_mgr.unlocked_add_remote("127.0.0.1:1");
  heartbeat_mgr.unlocked_add_remote("127.0.0.1:2");
  heartbeat_mgr.unlocked_add_remote("127.0.0.1:2");

  // Test heartbeat sending
  //
  // To check that all heartbeats are sent, we encode the sent packets into
  // strings and add them to a set.
  std::this_thread::sleep_for(std::chrono::microseconds(static_cast<size_t>(
      2 * to_usec(heartbeat_mgr.hb_send_delta_tsc_, kTestFreqGhz))));  // wiggle
  heartbeat_mgr.do_one(failed_uris);

  assert(sent_vec.size() >= 3);
  std::set<std::string> sh;  // Sent heartbeats

  for (auto &sm_pkt : sent_vec) {
    sh.insert(sm_pkt.client_.uri() + "-" + sm_pkt.server_.uri());
  }
  assert(sh.count(std::string(kTestLocalUri) + "-" + "127.0.0.1:1") > 0);
  assert(sh.count(std::string(kTestLocalUri) + "-" + "127.0.0.1:2") > 0);
  assert(sh.count(std::string(kTestLocalUri) + "-" + "127.0.0.1:2") > 0);

  // Test failure timeout for the latter two URIs
  std::this_thread::sleep_for(std::chrono::microseconds(static_cast<size_t>(
      2 * kTestMachineFailureTimeoutMs * 1000)));  // x2 for wiggle-room

  // Receive a heartbeat from remote "127.0.0.1:1".
  //
  // The test can fail if this process gets prempted for
  // kTestMachineFailureTimeoutMs between unlocked_receive_hb() and do_one()
  heartbeat_mgr.unlocked_receive_hb(
      HeartbeatMgr::make_heartbeat("127.0.0.1", 1, kTestLocalUri));
  heartbeat_mgr.do_one(failed_uris);

  assert(failed_uris.size() == 2);
  assert(!str_vec_contains(failed_uris, "127.0.0.1:1"));
  assert(str_vec_contains(failed_uris, "127.0.0.1:2"));
  assert(str_vec_contains(failed_uris, "127.0.0.1:2"));

  // Now wait for the first URI to time out
  failed_uris.clear();
  std::this_thread::sleep_for(std::chrono::microseconds(static_cast<size_t>(
      2 * kTestMachineFailureTimeoutMs * 1000)));  // x2 for wiggle-room
  heartbeat_mgr.do_one(failed_uris);
  assert(failed_uris.size() == 1);
  assert(str_vec_contains(failed_uris, "127.0.0.1:1"));

  // All servers have failed, so no heartbeats should be sent from now
  sent_vec.clear();
  failed_uris.clear();
  std::this_thread::sleep_for(std::chrono::microseconds(static_cast<size_t>(
      2 * kTestMachineFailureTimeoutMs * 1000)));  // x2 for wiggle-room
  heartbeat_mgr.do_one(failed_uris);
  assert(sent_vec.empty());
  assert(failed_uris.empty());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
