#include <gtest/gtest.h>

#define private public
#include "pinger.h"

using namespace erpc;

static double kTestFreqGhz = 2.5;
static double kTestMachineFailureTimeoutMs = 1;

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

TEST(PingerTest, Client) {
  Pinger pinger(kTestFreqGhz, kTestMachineFailureTimeoutMs);
  pinger.ping_udp_client.enable_recording();

  pinger.unlocked_add_remote_server("server_1:1");
  usleep(2 * kTestMachineFailureTimeoutMs * 1000);  // x2 for wiggle-room

  std::vector<std::string> failed_servers;
  pinger.do_one(failed_servers);

  // Check the pinger's sent_vec
  assert(failed_servers.size() == 1);
  assert(failed_servers.front() == "server_1:1");
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
