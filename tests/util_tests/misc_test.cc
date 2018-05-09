#include <gtest/gtest.h>
#include <limits.h>
#include "util/math_utils.h"

// Misc tests
TEST(StdDevTest, All) {
  std::vector<double> vec;
  vec.push_back(1.0);
  ASSERT_EQ(erpc::stddev(vec), 0.0);

  vec.push_back(1.0);
  ASSERT_EQ(erpc::stddev(vec), 0.0);

  // vec.push_back(2.0);
  // ASSERT_DOUBLE_EQ(erpc::stddev(vec), 0.47140452079103);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
