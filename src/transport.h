/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include <functional>
#include "common.h"
#include "msg_buffer.h"
#include "pkthdr.h"
#include "util/buffer.h"
#include "util/math_utils.h"

namespace erpc {

class HugeAlloc;  // Forward declaration: HugeAlloc needs MemRegInfo

/// Generic mostly-reliable transport
class Transport {
 public:
  static constexpr size_t kMaxRoutingInfoSize = 32;  ///< Space for routing info
  static constexpr size_t kMaxMemRegInfoSize = 64;   ///< Space for mem reg info

  // Queue depths
  static constexpr size_t kRecvQueueDepth = 2048;  ///< RECV queue size

  static_assert(is_power_of_two<size_t>(kRecvQueueDepth), "");

  /**
   * @brief Generic struct to store routing info for any transport.
   *
   * This can contain both cluster-wide valid members (e.g., {LID, QPN}), and
   * members that are only locally valid (e.g., a pointer to \p ibv_ah).
   */
  struct RoutingInfo {
    uint8_t buf[kMaxRoutingInfoSize];
  };

  /// Generic struct to store memory registration info for any transport.
  struct MemRegInfo {
    void* transport_mr;  ///< The transport-specific memory region (eg, ibv_mr)
    uint32_t lkey;       ///< The lkey of the memory region

    MemRegInfo(void* transport_mr, uint32_t lkey)
        : transport_mr(transport_mr), lkey(lkey) {}

    MemRegInfo() : transport_mr(nullptr), lkey(0xffffffff) {}
  };

  /// Info about a packet that needs TX. Using void* instead of MsgBuffer* is
  /// not straightforward since we need to locate packet headers.
  struct tx_burst_item_t {
    RoutingInfo* routing_info;  ///< Routing info for this packet
    MsgBuffer* msg_buffer;      ///< MsgBuffer for this packet
    size_t offset;              ///< Offset for this packet in the MsgBuffer
    size_t data_bytes;          ///< Number of data bytes to TX from \p offset
    bool drop;                  ///< Drop this packet. Only used with kTesting.
  };

  /// Generic types for memory registration and deregistration functions.
  typedef std::function<MemRegInfo(void*, size_t)> reg_mr_func_t;
  typedef std::function<void(MemRegInfo)> dereg_mr_func_t;

  enum class TransportType { kInfiniBand, kRoCE, kRaw, kInvalid };

  static std::string get_name(TransportType transport_type) {
    switch (transport_type) {
      case TransportType::kInfiniBand:
        return "[InfiniBand]";
      case TransportType::kRoCE:
        return "[RoCE]";
      case TransportType::kRaw:
        return "[Raw Ethernet]";
      case TransportType::kInvalid:
        return "[Invalid]";
    }
    throw std::runtime_error("eRPC: Invalid transport");
  }

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
  Transport(TransportType transport_type, uint8_t rpc_id, uint8_t phy_port);

  /**
   * @brief Initialize transport structures that require hugepages, and fill
   * the RX ring.
   *
   * @throw runtime_error if initialization fails. This exception is caught
   * in the parent Rpc, which then deletes \p huge_alloc so we don't need to.
   */
  void init_hugepage_structures(HugeAlloc* huge_alloc, uint8_t** rx_ring);

  /// Initialize the memory registration and deregistratin functions
  void init_mem_reg_funcs();

  ~Transport();

  /**
   * @brief Transmit a batch of packets
   *
   * Multiple packets may belong to the same msgbuf; burst items contain
   * offsets into a msgbuf.
   *
   * @param tx_burst_arr Info about the packets to TX
   * @param num_pkts The total number of packets to transmit (<= \p kPostlist)
   */
  void tx_burst(const tx_burst_item_t* tx_burst_arr, size_t num_pkts);

  /// Flush the transmit queue, returning ownership of all SEND WQE buffers
  /// back to eRPC. This can be quite expensive (several microseconds).
  void tx_flush();

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
   * @brief Try to resolve routing information received from a remote host. The
   * remote, cluster-wide meaningful info is available in \p routing_info. The
   * resolved, locally-meaningful info is also stored in \p routing_info.
   *
   * For example, for InfiniBand, this involves creating the address handle
   * using the remote port LID.
   *
   * @return True if resolution succeeds, false otherwise.
   */
  bool resolve_remote_routing_info(RoutingInfo* routing_info) const;

  /// Return a string representation of \p routing_info
  static std::string routing_info_str(RoutingInfo* routing_info);

  // Members that are needed by all transports. Constructor args first.
  const TransportType transport_type;
  const uint8_t rpc_id;    // Debug-only
  const uint8_t phy_port;  ///< 0-based physical port specified by application

  // Other members
  reg_mr_func_t reg_mr_func;      ///< The memory registration function
  dereg_mr_func_t dereg_mr_func;  ///< The memory deregistration function

  // Members initialized after the hugepage allocator is provided
  HugeAlloc* huge_alloc;  ///< The parent Rpc's hugepage allocator
  size_t numa_node;       ///< Derived from \p huge_alloc

  struct {
    size_t tx_flush_count = 0;  ///< Number of times tx_flush() has been called
  } testing;
};

}  // End erpc

#endif  // ERPC_TRANSPORT_H
