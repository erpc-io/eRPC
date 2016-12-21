#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

namespace ERpc {

// Generic transport class
class Transport {
public:
  Transport();
  ~Transport();

  virtual void send_message();
  virtual void poll_completions();
};

} // End ERpc

#endif //ERPC_TRANSPORT_H
