#ifndef ERPC_RPC_H_H
#define ERPC_RPC_H_H

#include "transport.h"

namespace ERpc {

class Rpc {
public:
  Rpc();
  ~Rpc();

  void foo() {
    ERpc::Transport transport;
    transport.send_message();
  }
};

} // End ERpc

#endif //ERPC_RPC_H_H
