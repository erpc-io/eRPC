#include <gtest/gtest.h>
#include <algorithm>

#define private public
#include "pinger.h"

using namespace erpc;

static constexpr double kTestFreqGhz = 2.5;
static constexpr double kTestMachineFailureTimeoutMs = 1;
static constexpr const char *kTestLocalHostname = "localhost";
static constexpr uint16_t kTestLocalSmUdpPort = 31850;

/// Return true iff vec contains s
static bool str_vec_contains(const std::vector<std::string> &vec,
                             const std::string &s) {
  return std::find(vec.begin(), vec.end(), s) != vec.end();
}

TEST(PingerTest, PriorityQueueOrderTest) {
  std::priority_queue<Pinger::PingEvent, std::vector<Pinger::PingEvent>,
                      Pinger::PingEventComparator>
      ping_event_queue;
  ping_event_queue.emplace(Pinger::PingEventType::kSend, "hostname_1", 1);
  ping_event_queue.emplace(Pinger::PingEventType::kCheck, "hostname_2", 3);
  ping_event_queue.emplace(Pinger::PingEventType::kSend, "hostname_3", 2);

  assert(ping_event_queue.top().tsc == 1);
  ping_event_queue.pop();

  assert(ping_event_queue.top().tsc == 2);
  ping_event_queue.pop();

  assert(ping_event_queue.top().tsc == 3);
  ping_event_queue.pop();
}

TEST(PingerTest, URISplitTest) {
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

TEST(PingerTest, Client) {
  Pinger pinger(kTestLocalHostname, kTestLocalSmUdpPort, kTestFreqGhz,
                kTestMachineFailureTimeoutMs);
  pinger.ping_udp_client.enable_recording();

  pinger.unlocked_add_remote_server("server_1:1");
  pinger.unlocked_add_remote_server("server_2:1");
  pinger.unlocked_add_remote_server("server_1:2");

  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room

  std::vector<std::string> failed_uris;
  pinger.do_one(failed_uris);

  assert(failed_uris.size() == 3);
  assert(str_vec_contains(failed_uris, "server_1:1"));
  assert(str_vec_contains(failed_uris, "server_2:1"));
  assert(str_vec_contains(failed_uris, "server_1:2"));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
