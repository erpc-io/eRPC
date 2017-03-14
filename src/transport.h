/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include <string>
#include "common.h"
#include "msg_buffer.h"
#include "pkthdr.h"
#include "session.h"
#include "transport_types.h"
#include "util/buffer.h"
#include "util/huge_alloc.h"

namespace ERpc {

/// Generic mostly-reliable transport
class Transport {
 public:
  // Queue depths
  static const size_t kRecvQueueDepth = 2048;  ///< RECV queue size
  static const size_t kSendQueueDepth = 128;   ///< SEND queue size

  static_assert(is_power_of_two<size_t>(kRecvQueueDepth), "");
  static_assert(is_power_of_two<size_t>(kSendQueueDepth), "");

  /**
   * @brief Partially construct the transport object without using eRPC's
   * hugepage allocator. The device driver is allowed to use its own hugepages
   * (with a different allocator).
   *
   * This function must initialize \p reg_mr_func and \p dereg_mr_func, which
   * will be used to construct the allocator.
   *
   * @throw runtime_error if creation fails
   */
  Transport(TransportType transport_type, uint8_t app_tid, uint8_t phy_port);

  /**
   * @brief Initialize transport structures that require hugepages, and fill
   * the RX ring.
   *
   * @throw runtime_error if initialization fails. This exception is caught
   * in the parent Rpc, which then deletes \p huge_alloc so we don't need to.
   */
  void init_hugepage_structures(HugeAllocator* huge_alloc, uint8_t** rx_ring);

  /// Initialize the memory registration and deregistratin functions
  void init_mem_reg_funcs();

  ~Transport();

  /**
   * @brief Transmit a batch of packets
   *
   * Multiple packets may belong to the same MsgBuffer. The transport determines
   * the offset of each of these packets using \p offset_arr.
   *
   * @param tx_burst_arr Info about the packets to TX
   * @param num_pkts The total number of packets to transmit (<= \p kPostlist)
   */
  void tx_burst(const tx_burst_item_t* tx_burst_arr, size_t num_pkts);

  /**
   * @brief The generic packet reception function
   *
   * This function returns the number of new packets available in the RX ring.
   *
   * The Rpc layer controls posting of RECV descriptors explicitly using
   * the post_recvs() function.
   */
  size_t rx_burst();

  /**
   * @brief Post RECVs to the receive queue
   *
   * @param num_recvs The zero or more RECVs to post
   */
  void post_recvs(size_t num_recvs);

  /// Fill-in local routing information
  void fill_local_routing_info(RoutingInfo* routing_info) const;

  /**
   * @brief Try to resolve routing information received from remote host. (e.g.,
   * for InfiniBand, this involves creating the address handle using the remote
   * port LID.)
   *
   * @return True if resolution succeeds, false otherwise.
   */
  bool resolve_remote_routing_info(RoutingInfo* routing_info) const;

  /// Return a string representation of \p routing_info
  static std::string routing_info_str(RoutingInfo* routing_info);

  // Members that are needed by all transports. Constructor args first.
  const TransportType transport_type;
  const uint8_t app_tid;   /* Debug-only */
  const uint8_t phy_port;  ///< 0-based physical port specified by application

  // Other members
  reg_mr_func_t reg_mr_func;      ///< The memory registration function
  dereg_mr_func_t dereg_mr_func;  ///< The memory deregistration function

  // Members initialized after the hugepage allocator is provided
  HugeAllocator* huge_alloc;  ///< The parent Rpc's hugepage allocator
  size_t numa_node;           ///< Derived from \p huge_alloc
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H
