/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include <string>
#include "common.h"
#include "huge_alloc.h"
#include "session.h"
#include "transport_types.h"
#include "util/buffer.h"

namespace ERpc {

static const size_t kRecvQueueDepth = 2048;  ///< RECV queue size
static const size_t kSendQueueDepth = 128;   ///< SEND queue size
static const size_t kPostlist = 16;          ///< Maximum post list size
static_assert(is_power_of_two<size_t>(kRecvQueueDepth), "");
static_assert(is_power_of_two<size_t>(kSendQueueDepth), "");

/// Generic mostly-reliable transport
class Transport {
 public:
  Transport(TransportType transport_type, size_t mtu, uint8_t app_tid,
            uint8_t phy_port, HugeAllocator *huge_alloc);

  ~Transport();

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

  /// Fill-in the transport-specific routing information
  void fill_routing_info(RoutingInfo *routing_info) const;

  /// Return a string representation of \p routing_info
  std::string routing_info_str(RoutingInfo *routing_info) const;

  // Members that are needed by all transports. Constructor args first.
  const TransportType transport_type;
  const size_t mtu;
  const uint8_t app_tid; /* Debug-only */
  const uint8_t phy_port;
  HugeAllocator *huge_alloc; /* The parent Rpc's hugepage allocator */

  // Derived members
  const size_t numa_node; /* Derived from @huge_alloc */
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H
