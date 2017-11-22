#include "system_tests.h"

namespace erpc {
class RpcRxTest : public RpcTest {};

//
// process_comps_st()
//
TEST_F(RpcRxTest, process_comps_st) {}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
