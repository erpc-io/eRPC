/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include <string>
#include "common.h"
#include "session.h"
#include "transport_types.h"
#include "util/buffer.h"
#include "util/huge_alloc.h"

namespace ERpc {

/// Generic mostly-reliable transport
class Transport {
 public:
  static const size_t kRecvQueueDepth = 2048;  ///< RECV queue size
  static const size_t kSendQueueDepth = 128;   ///< SEND queue size
  static const size_t kPostlist = 16;          ///< Maximum post list size
  static_assert(is_power_of_two<size_t>(kRecvQueueDepth), "");
  static_assert(is_power_of_two<size_t>(kSendQueueDepth), "");

  Transport(TransportType transport_type, size_t mtu, uint8_t app_tid,
            uint8_t phy_port);

  /// Initialize transport structures that require hugepages.
  /// Throws \p runtime_error if initialization fails. This exception is caught
  /// in the creator Rpc, which then deletes \p huge_alloc.
  /// XXX: Fix documentation style
  void init_hugepage_structures(HugeAllocator *huge_alloc);

  /// Initialize the memory registration and deregistratin functions
  void init_mem_reg_funcs();

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

  // Other members
  reg_mr_func_t reg_mr_func;      ///< The memory registration function
  dereg_mr_func_t dereg_mr_func;  ///< The memory deregistration function

  // Members initialized after the hugepage allocator is provided
  HugeAllocator *huge_alloc;  ///< The parent Rpc's hugepage allocator
  size_t numa_node;           ///< Derived from \p huge_alloc
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H
