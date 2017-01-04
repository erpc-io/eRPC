#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <mutex>
#include <queue>
#include <vector>
#include "common.h"
#include "session.h"
using namespace std;

namespace ERpc {

class Nexus {
 public:
  Nexus();
  ~Nexus();

  void register_hook(SessionManagementHook *hook);
  void unregister_hook(SessionManagementHook *hook);

 private:
  std::vector<volatile SessionManagementHook *> reg_hooks;
};

}  // End ERpc

#endif  // ERPC_RPC_H
