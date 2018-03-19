#include <gtest/gtest.h>

//#define private public
#include "transport_impl/raw/raw_transport.h"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
