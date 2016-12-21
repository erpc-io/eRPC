#ifndef ERPC_INFINIBAND_H_H
#define ERPC_INFINIBAND_H_H

#include "../transport.h"

class InfinibandTransport: public Transport {
public:
  InfinibandTransport();
  ~InfinibandTransport();

  virtual void send_message() {
  }

  virtual void poll_completions() {

  }

};


#endif //ERPC_INFINIBAND_H_H
