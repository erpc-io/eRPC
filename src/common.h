#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

// Header file that is included everywhere
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define forceinline inline __attribute__((always_inline))
#define _unused(x) ((void)(x)) /* Make production build happy */

#endif
