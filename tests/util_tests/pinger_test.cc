#include <gtest/gtest.h>
#include <algorithm>

#define private public
#include "pinger.h"

using namespace erpc;

static constexpr double kTestFreqGhz = 2.5;
static constexpr double kTestMachineFailureTimeoutMs = 1;
static constexpr const char *kTestLocalHostname = "localhost";
static constexpr uint16_t kTestLocalSmUdpPort = 31850;
static constexpr const char *kTestLocalUri = "localhost:31850";

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
  std::vector<std::string> failed_uris;

  pinger.ping_udp_client.enable_recording();
  std::vector<SmPkt> &sent_vec = pinger.ping_udp_client.sent_vec;

  // The client will sent actual UDP packets, so we need valid addresses
  pinger.unlocked_add_remote_server("127.0.0.1:1");
  pinger.unlocked_add_remote_server("127.0.0.1:2");
  pinger.unlocked_add_remote_server("localhost:2");

  // Test ping sending. To check that all pings are sent, we encode the
  // sent packets into strings and add them to a set.
  usleep(2 * to_usec(pinger.ping_send_delta_tsc, kTestFreqGhz));  // wiggle-room
  pinger.do_one(failed_uris);

  assert(sent_vec.size() >= 3);
  std::set<std::string> SP;  // Sent pings

  for (auto &sm_pkt : sent_vec) {
    SP.insert(sm_pkt.client.uri() + "-" + sm_pkt.server.uri());
  }
  assert(SP.count(std::string(kTestLocalUri) + "-" + "127.0.0.1:1") > 0);
  assert(SP.count(std::string(kTestLocalUri) + "-" + "127.0.0.1:2") > 0);
  assert(SP.count(std::string(kTestLocalUri) + "-" + "localhost:2") > 0);

  // Test failure timeout
  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room
  pinger.do_one(failed_uris);

  assert(failed_uris.size() == 3);
  assert(str_vec_contains(failed_uris, "127.0.0.1:1"));
  assert(str_vec_contains(failed_uris, "127.0.0.1:2"));
  assert(str_vec_contains(failed_uris, "localhost:2"));

  // All servers have failed, so no pings should be sent from now
  sent_vec.clear();
  failed_uris.clear();
  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room
  pinger.do_one(failed_uris);
  assert(sent_vec.empty());
  assert(failed_uris.empty());
}

TEST(PingerTest, Server) {
  Pinger pinger(kTestLocalHostname, kTestLocalSmUdpPort, kTestFreqGhz,
                kTestMachineFailureTimeoutMs);
  std::vector<std::string> failed_uris;

  pinger.ping_udp_client.enable_recording();

  // The server never sends pings, so any addresses are fine
  pinger.unlocked_add_remote_client("client_1:1");
  pinger.unlocked_add_remote_client("client_1:2");
  pinger.unlocked_add_remote_client("client_2:3");

  // Test failure timeout
  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room
  pinger.do_one(failed_uris);

  assert(failed_uris.size() == 3);
  assert(str_vec_contains(failed_uris, "client_1:1"));
  assert(str_vec_contains(failed_uris, "client_1:2"));
  assert(str_vec_contains(failed_uris, "client_2:3"));

  // The server should never send pings
  assert(pinger.ping_udp_client.sent_vec.empty());

  // All clients have failed, so no failures should be detected from now
  failed_uris.clear();
  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room
  pinger.do_one(failed_uris);
  assert(failed_uris.empty());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
