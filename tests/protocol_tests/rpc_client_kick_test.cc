#include "protocol_tests.h"

namespace erpc {}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
