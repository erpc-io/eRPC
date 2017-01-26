/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include "common.h"
#include "session.h"
#include "transport_types.h"
#include "util/buffer.h"

namespace ERpc {

/**
 * @brief Generic mostly-reliable transport
 */
class Transport {
 public:
  void fill_routing_info(RoutingInfo *routing_info) const;

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

  TransportType transport_type;
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H
