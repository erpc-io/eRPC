#ifndef ERPC_INFINIBAND_TRANSPORT_H
#define ERPC_INFINIBAND_TRANSPORT_H

#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

class InfiniBandTransport : public Transport {
 public:
  InfiniBandTransport();
  ~InfiniBandTransport();

  void fill_routing_info(RoutingInfo *routing_info) const;

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();
};

}  // End ERpc

#endif  // ERPC_INFINIBAND_TRANSPORT_H
