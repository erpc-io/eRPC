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
  /// Min MTU for any transport. Smaller than 4096 for prime RECV cachelines.
  static const size_t kMinMtu = 3800;
  static const size_t kPostlist = 16;  ///< Maximum post list size

  // Queue depths
  static const size_t kRecvQueueDepth = 2048;  ///< RECV queue size
  static const size_t kSendQueueDepth = 128;   ///< SEND queue size

  static_assert(is_power_of_two<size_t>(kRecvQueueDepth), "");
  static_assert(is_power_of_two<size_t>(kSendQueueDepth), "");

  // Packet header
  static const size_t kMsgSizeBits = 24;  ///< Bits for message size
  static const size_t kPktNumBits = 13;   ///< Bits for packet number in request
  static const size_t kReqNumBits = 44;   ///< Bits for request number
  static const size_t kPktHdrMagicBits = 4;  ///< Debug bits for magic number
  static const size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

  static_assert(kPktHdrMagic < (1ull << kPktHdrMagicBits), "");

  struct pkthdr_t {
    uint8_t req_type;        /// RPC request type
    uint64_t msg_size : 24;  ///< Total req/resp msg size, excluding headers
    uint64_t rem_session_num : 16;  ///< Session number of the remote session
    uint64_t is_req : 1;            ///< 1 if this packet is a request packet
    uint64_t is_first : 1;     ///< 1 if this packet is the first message packet
    uint64_t is_expected : 1;  ///< 1 if this packet is an "expected" packet
    uint64_t pkt_num : kPktNumBits;     ///< Packet number in the request
    uint64_t req_num : kReqNumBits;     ///< Request number of this packet
    uint64_t magic : kPktHdrMagicBits;  ///< Magic from alloc_pkt_buffer()
  };
  static_assert(sizeof(pkthdr_t) == 16, "");

  Transport(TransportType transport_type, size_t mtu, uint8_t app_tid,
            uint8_t phy_port);

  /**
   * @brief Initialize transport structures that require hugepages
   * @throw runtime_error if initialization fails. This exception is caught
   * in the parent Rpc, which then deletes \p huge_alloc so we don't need to.
   */
  void init_hugepage_structures(HugeAllocator* huge_alloc);

  /// Initialize the memory registration and deregistratin functions
  void init_mem_reg_funcs();

  ~Transport();

  /**
   * @brief The generic packet transmission function
   *
   * For each packet, a PktBuffer is specified. Packets for which 
   *
   * Packets for which the offset is non-zero use 2 DMAs (header and data).
   * Small packets have \p offset = 0, and use inline or single-DMA transfers.
   */
  void tx_burst(RoutingInfo const* const* routing_info_arr,
                Buffer const* const* pkt_buffer_arr, size_t const* offset_arr,
                size_t num_pkts);

  /**
   * @brief The generic packet reception function
   *
   * This function returns pointers to the transport's RECV DMA ring buffers,
   * so it must not re-post the returned ring buffers to the NIC (because then
   * the NIC could overwrite the buffers while the Rpc layer  is processing
   * them.
   *
   * The Rpc layer controls posting of RECV descriptors explicitly using
   * the post_recvs() function.
   */
  void rx_burst(Buffer* pkt_buffer_arr, size_t* num_pkts);

  /// Post RECVs to the receive queue after processing \p rx_burst results
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
  const size_t mtu;
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
