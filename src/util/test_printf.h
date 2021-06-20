#pragma once
#include <stdio.h>

#define test_printf(...)     \
  do {                       \
    printf("[          ] "); \
    printf(__VA_ARGS__);     \
    fflush(stderr);          \
    fflush(stdout);          \
  } while (0)
