#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, rpc_list_test) {
  SSlot a, b, c, d;
  rpc_->add_to_active_rpc_list(a);
  rpc_->add_to_active_rpc_list(b);
  rpc_->add_to_active_rpc_list(c);
  rpc_->add_to_active_rpc_list(d);

  rpc_->delete_from_active_rpc_list(d);
  rpc_->delete_from_active_rpc_list(b);

  ASSERT_EQ(rpc_->active_rpcs_root_sentinel_.client_info_.next_, &a);
  ASSERT_EQ(rpc_->active_rpcs_tail_sentinel_.client_info_.prev_, &c);

  ASSERT_EQ(a.client_info_.next_, &c);
  ASSERT_EQ(a.client_info_.prev_, &rpc_->active_rpcs_root_sentinel_);

  ASSERT_EQ(c.client_info_.prev_, &a);
  ASSERT_EQ(c.client_info_.next_, &rpc_->active_rpcs_tail_sentinel_);

  rpc_->delete_from_active_rpc_list(a);
  rpc_->delete_from_active_rpc_list(c);
  ASSERT_EQ(rpc_->active_rpcs_root_sentinel_.client_info_.next_,
            &rpc_->active_rpcs_tail_sentinel_);
  ASSERT_EQ(rpc_->active_rpcs_tail_sentinel_.client_info_.prev_,
            &rpc_->active_rpcs_root_sentinel_);
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
