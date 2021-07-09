/**
 * @file dpdk_ownership_memzone_test.cc
 * @brief Tests for the DPDK queue pair ownership memzone
 */
#ifdef ERPC_DPDK

#include <gtest/gtest.h>

#define private public
#include "transport_impl/dpdk/dpdk_transport.h"

namespace erpc {
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kProc0RandomId = 11;
static constexpr size_t kProc1RandomId = 22;

// gtest does not like static constexprs
const size_t k_max_queues_per_port = DpdkTransport::kMaxQueuesPerPort;
const size_t k_invalid_qp_id = DpdkTransport::kInvalidQpId;

TEST(DpdkOwnershipMemzoneTest, basic) {
  // Mimic DPDK memzone
  auto *omz = reinterpret_cast<DpdkTransport::ownership_memzone_t *>(
      malloc(sizeof(DpdkTransport::ownership_memzone_t)));
  omz->init();

  ASSERT_EQ(omz->get_epoch(), 0);
  ASSERT_EQ(omz->get_num_qps_available(), k_max_queues_per_port);

  size_t qp_id = omz->get_qp(kTestPhyPort, kProc0RandomId);
  ASSERT_EQ(qp_id, 0);
  ASSERT_EQ(omz->get_num_qps_available(), k_max_queues_per_port - 1);

  // Try to free an already-free QP
  int ret = omz->free_qp(kTestPhyPort, 1);
  ASSERT_EQ(ret, EALREADY);
  ASSERT_EQ(omz->get_num_qps_available(), k_max_queues_per_port - 1);

  // Free QP 0
  ret = omz->free_qp(kTestPhyPort, 0);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(omz->get_num_qps_available(), k_max_queues_per_port);

  // Pretend like QP 0 is owned by a different process, then try to free it
  omz->owner_[kTestPhyPort][0].pid_ = INT_MAX;
  omz->num_qps_available_--;
  ret = omz->free_qp(kTestPhyPort, 0);
  ASSERT_EQ(ret, EPERM);

  free(omz);
}

TEST(DpdkOwnershipMemzoneTest, ran_out) {
  // Mimic DPDK memzone
  auto *omz = reinterpret_cast<DpdkTransport::ownership_memzone_t *>(
      malloc(sizeof(DpdkTransport::ownership_memzone_t)));
  omz->init();

  // Reserve all QPs and then try to get another
  for (size_t i = 0; i < DpdkTransport::kMaxQueuesPerPort; i++) {
    int ret = omz->get_qp(kTestPhyPort, kProc0RandomId);
    ASSERT_EQ(ret, i);
  }
  int ret = omz->get_qp(kTestPhyPort, kProc0RandomId);
  ASSERT_EQ(ret, k_invalid_qp_id);
  ASSERT_EQ(omz->num_qps_available_, 0);

  // Free QP #3 and check if we can re-get it
  static_assert(DpdkTransport::kMaxQueuesPerPort >= 4, "");
  omz->free_qp(kTestPhyPort, 3);
  ASSERT_EQ(omz->get_num_qps_available(), 1);

  ret = omz->get_qp(kTestPhyPort, kProc0RandomId);
  ASSERT_EQ(ret, 3);
  ASSERT_EQ(omz->get_num_qps_available(), 0);
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#endif
