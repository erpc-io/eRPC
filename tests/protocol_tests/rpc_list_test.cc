#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, rpc_list_test) {
  SSlot a, b, c, d;
  rpc->add_to_active_rpc_list(a);
  rpc->add_to_active_rpc_list(b);
  rpc->add_to_active_rpc_list(c);
  rpc->add_to_active_rpc_list(d);

  rpc->delete_from_active_rpc_list(d);
  rpc->delete_from_active_rpc_list(b);

  ASSERT_EQ(rpc->active_rpcs_root_sentinel.client_info.next, &a);
  ASSERT_EQ(rpc->active_rpcs_tail_sentinel.client_info.prev, &c);

  ASSERT_EQ(a.client_info.next, &c);
  ASSERT_EQ(a.client_info.prev, &rpc->active_rpcs_root_sentinel);

  ASSERT_EQ(c.client_info.prev, &a);
  ASSERT_EQ(c.client_info.next, &rpc->active_rpcs_tail_sentinel);

  rpc->delete_from_active_rpc_list(a);
  rpc->delete_from_active_rpc_list(c);
  ASSERT_EQ(rpc->active_rpcs_root_sentinel.client_info.next,
            &rpc->active_rpcs_tail_sentinel);
  ASSERT_EQ(rpc->active_rpcs_tail_sentinel.client_info.prev,
            &rpc->active_rpcs_root_sentinel);
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
