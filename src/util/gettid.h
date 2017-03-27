#ifndef ERPC_GETTID_H
#define ERPC_GETTID_H

#include <sys/syscall.h>
#include <unistd.h>

/// Return the calling thread's ID
static inline int gettid() {
  static __thread int tid = -1;

  if (tid == -1) {
    tid = (int)syscall(SYS_gettid);
  }

  return tid;
}

#endif
