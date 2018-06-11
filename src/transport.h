/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#pragma once

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
  static constexpr size_t kNumRxRingEntries = 4096;
  static_assert(is_power_of_two<size_t>(kNumRxRingEntries), "");

  static constexpr size_t kMaxRoutingInfoSize = 48;  ///< Space for routing info
  static constexpr size_t kMaxMemRegInfoSize = 64;   ///< Space for mem reg info

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

  /// Info about a packet to transmit
  struct tx_burst_item_t {
    RoutingInfo* routing_info;  ///< Routing info for this packet
    MsgBuffer* msg_buffer;      ///< MsgBuffer for this packet

    size_t pkt_idx;  /// Packet index (not pkt_num) in msg_buffer to transmit
    size_t* tx_ts = nullptr;  ///< TX timestamp, only for congestion control
    bool drop;                ///< Drop this packet. Used only with kTesting.
  };

  /// Generic types for memory registration and deregistration functions.
  typedef std::function<MemRegInfo(void*, size_t)> reg_mr_func_t;
  typedef std::function<void(MemRegInfo)> dereg_mr_func_t;

  enum class TransportType { kInfiniBand, kRoCE, kRaw, kDPDK, kInvalid };

  static std::string get_name(TransportType transport_type) {
    switch (transport_type) {
      case TransportType::kInfiniBand:
        return "[InfiniBand]";
      case TransportType::kRoCE:
        return "[RoCE]";
      case TransportType::kRaw:
        return "[Raw Ethernet]";
      case TransportType::kDPDK:
        return "[DPDK]";
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
   * @param rpc_id The RPC ID of the parent RPC
   *
   * @throw runtime_error if creation fails
   */
  Transport(TransportType, uint8_t rpc_id, uint8_t phy_port, size_t numa_node,
            FILE* trace_file);

  /**
   * @brief Initialize transport structures that require hugepages, and
   * fill/save the RX ring.
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

  /// Complete pending TX DMAs, returning ownership of all TX buffers to eRPC
  void tx_flush();

  /**
   * @brief The generic packet RX function
   *
   * @return the number of new packets available in the RX ring. The Rpc layer
   * controls posting of RECVs explicitly using post_recvs().
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
   * Overwriting the original contents of \p routing_info is allowed.
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
  const uint8_t rpc_id;    ///< The parent Rpc's ID
  const uint8_t phy_port;  ///< 0-based index among active fabric ports
  const size_t numa_node;  ///< Derived from \p huge_alloc

  // Other members
  reg_mr_func_t reg_mr_func;      ///< The memory registration function
  dereg_mr_func_t dereg_mr_func;  ///< The memory deregistration function

  // Members initialized after the hugepage allocator is provided
  HugeAlloc* huge_alloc;  ///< The parent Rpc's hugepage allocator
  FILE* trace_file;       ///< The parent Rpc's high-verbosity log file

  struct {
    size_t tx_flush_count = 0;  ///< Number of times tx_flush() has been called
  } testing;
};
}  // End erpc
