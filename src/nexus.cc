#include "nexus.h"
#include <algorithm>
#include "common.h"
#include "rpc.h"

namespace ERpc {

Nexus::Nexus(int udp_port) : udp_port(udp_port) {}

Nexus::~Nexus() {}

void Nexus::register_hook(SessionManagementHook *hook) {
  assert(hook != NULL);

  /* The hook must not exist */
  if (std::find(reg_hooks.begin(), reg_hooks.end(), hook) != reg_hooks.end()) {
    fprintf(stderr, "eRPC Nexus: FATAL attempt to re-register hook %p\n", hook);
    exit(-1);
  }
  reg_hooks.push_back(hook);
}

void Nexus::unregister_hook(SessionManagementHook *hook) {
  assert(hook != NULL);

  /* The hook must exist in the vector of registered hooks */
  if (std::find(reg_hooks.begin(), reg_hooks.end(), hook) == reg_hooks.end()) {
    fprintf(stderr,
            "eRPC Nexus: FATAL attempt to unregister non-existent hook %p\n",
            hook);
    exit(-1);
  }

  reg_hooks.erase(std::remove(reg_hooks.begin(), reg_hooks.end(), hook),
                  reg_hooks.end());
}
}
