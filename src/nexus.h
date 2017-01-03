#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include "common.h"
#include "session.h"
#include <vector>
#include <mutex>
#include <queue>
using namespace std;

namespace ERpc {

class Rpc; // Forward declaration (class Rpc requires Nexus)

class Nexus {
 public:
  Nexus();
  ~Nexus();

 private:
  std::vector<Rpc*> registered_rpcs;
};

}  // End ERpc

#endif  // ERPC_RPC_H
