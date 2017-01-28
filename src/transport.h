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
#include "util/huge_alloc.h"

namespace ERpc {

static const size_t kRecvQueueSize = 2048;  ///< RECV queue size
static const size_t kSendQueueSize = 64;    ///< SEND queue size
static const size_t kPostlist = 16;         ///< Maximum post list size
static_assert(is_power_of_two<size_t>(kRecvQueueSize), "");
static_assert(is_power_of_two<size_t>(kSendQueueSize), "");

/// Generic mostly-reliable transport
class Transport {
 public:
  Transport(TransportType transport_type, uint8_t phy_port,
            HugeAllocator *huge_alloc)
      : transport_type(transport_type),
        phy_port(phy_port),
        huge_alloc(huge_alloc),
        numa_node(huge_alloc->get_numa_node()){};

  void fill_routing_info(RoutingInfo *routing_info) const;

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

  // Members that are needed by all transports
  const TransportType transport_type;
  const uint8_t phy_port;
  HugeAllocator *huge_alloc; /* The parent Rpc's hugepage allocator */
  const size_t numa_node;    /* Derived from @huge_alloc */
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H
