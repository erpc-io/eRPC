#include <gtest/gtest.h>
#include <limits.h>

#include "util/fixed_vector.h"
#include "util/test_printf.h"

TEST(FixedVectorTest, FixedVectorTest) {
  erpc::FixedVector<size_t, 4> fv;
  ASSERT_EQ(fv.capacity(), 4);

  fv.push_back(0);
  fv.push_back(1);
  fv.push_back(2);
  fv.push_back(3);
  ASSERT_EQ(fv.size(), 4);

  ASSERT_EQ(fv.pop_back(), 3);
  ASSERT_EQ(fv.pop_back(), 2);
  ASSERT_EQ(fv.pop_back(), 1);
  ASSERT_EQ(fv.pop_back(), 0);
  ASSERT_EQ(fv.size(), 0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
