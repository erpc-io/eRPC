#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include "transport.h"

namespace ERpc {

class Rpc {
public:
  Rpc();
  ~Rpc();

  void Foo() {
    Transport transport;
  }
};

} // End ERpc

#endif // ERPC_RPC_H
