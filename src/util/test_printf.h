#pragma once

#include <gtest/gtest.h>
#include <string>

namespace testing {
namespace internal {
enum GTestColor { COLOR_DEFAULT, COLOR_RED, COLOR_GREEN, COLOR_YELLOW };

extern void ColoredPrintf(GTestColor color, const char *fmt, ...);
}  // namespace internal
}  // namespace testing

#define test_printf(...)                                                    \
  do {                                                                      \
    testing::internal::ColoredPrintf(testing::internal::COLOR_GREEN,        \
                                     "[          ] ");                      \
    testing::internal::ColoredPrintf(testing::internal::COLOR_YELLOW,       \
                                     __VA_ARGS__);                          \
    testing::internal::ColoredPrintf(testing::internal::COLOR_DEFAULT, ""); \
    fflush(stderr);                                                         \
    fflush(stdout);                                                         \
  } while (0)
