#pragma once

#include "common.h"
#include "pkthdr.h"
#include "util/buffer.h"
#include "util/math_utils.h"

namespace erpc {

// Forward declarations for friendship
class IBTransport;
class RawTransport;
class DpdkTransport;
class Session;

template <typename T>
class Rpc;

/// A message buffer with magic-ful headers at the beginning and end
class MsgBuffer {
  friend class IBTransport;
  friend class RawTransport;
  friend class DpdkTransport;
  friend class Session;
  friend class Rpc<IBTransport>;
  friend class Rpc<RawTransport>;
  friend class Rpc<DpdkTransport>;

 public:
  MsgBuffer() {}
  ~MsgBuffer() {}

  /// Return a pointer to the pre-appended packet header of this MsgBuffer
  inline pkthdr_t *get_pkthdr_0() const {
    return reinterpret_cast<pkthdr_t *>(buf - sizeof(pkthdr_t));
  }

  /// Return a pointer to the nth packet header of this MsgBuffer.
  /// get_pkthdr_0() is more efficient for retrieving the zeroth header.
  inline pkthdr_t *get_pkthdr_n(size_t n) const {
    if (unlikely(n == 0)) return get_pkthdr_0();
    return reinterpret_cast<pkthdr_t *>(
        buf + round_up<sizeof(size_t)>(max_data_size) +
        (n - 1) * sizeof(pkthdr_t));
  }

  ///@{ Accessors for the packet header
  inline bool is_req() const { return get_pkthdr_0()->is_req(); }
  inline bool is_resp() const { return get_pkthdr_0()->is_resp(); }
  inline bool is_expl_cr() const { return get_pkthdr_0()->is_expl_cr(); }
  inline bool is_rfr() const { return get_pkthdr_0()->is_rfr(); }
  inline uint64_t get_req_num() const { return get_pkthdr_0()->req_num; }
  inline uint64_t get_pkt_type() const { return get_pkthdr_0()->pkt_type; }
  inline uint8_t get_req_type() const { return get_pkthdr_0()->req_type; }
  ///@}

  std::string get_pkthdr_str(size_t pkt_idx) const {
    return get_pkthdr_n(pkt_idx)->to_string();
  }

  /// Basic validity check that every MsgBuffer must satisfy
  inline bool is_valid() const {
    return buf != nullptr && get_pkthdr_0()->magic == kPktHdrMagic;
  }

  /// Return true iff this MsgBuffer uses a dynamically-allocated MsgBuffer.
  /// This function does not sanity-check other fields.
  inline bool is_dynamic() const { return buffer.buf != nullptr; }

  /// Check if this MsgBuffer is buried
  inline bool is_buried() const {
    return (buf == nullptr && buffer.buf == nullptr);
  }

  /// Return the current amount of data in the MsgBuffer. This can be smaller
  /// than the maximum data capacity of the MsgBuffer.
  inline size_t get_data_size() const { return data_size; }

  /// Get the packet size (i.e., including packet header) of a packet
  template <size_t kMaxDataPerPkt>
  inline size_t get_pkt_size(size_t pkt_idx) const {
    size_t offset = pkt_idx * kMaxDataPerPkt;
    return sizeof(pkthdr_t) + std::min(kMaxDataPerPkt, data_size - offset);
  }

  /// Return a string representation of this MsgBuffer
  std::string to_string() const {
    if (buf == nullptr) return "[Invalid]";

    std::ostringstream ret;
    ret << "[buf " << static_cast<void *>(buf) << ", "
        << "buffer " << buffer.to_string() << ", "
        << "data_size " << data_size << "(" << max_data_size << "), "
        << "pkts " << num_pkts << "(" << max_num_pkts << ")]";
    return ret.str();
  }

  /// Construct a MsgBuffer with a dynamic Buffer allocated by eRPC.
  /// The zeroth packet header is stored at \p buffer.buf. \p buffer must have
  /// space for at least \p max_data_bytes, and \p max_num_pkts packet headers.
  MsgBuffer(Buffer buffer, size_t max_data_size, size_t max_num_pkts)
      : buf(buffer.buf + sizeof(pkthdr_t)),
        buffer(buffer),
        max_data_size(max_data_size),
        data_size(max_data_size),
        max_num_pkts(max_num_pkts),
        num_pkts(max_num_pkts) {
    assert(buffer.buf != nullptr);  // buffer must be valid
    // data_size can be 0
    assert(max_num_pkts >= 1);
    assert(buffer.class_size >=
           max_data_size + max_num_pkts * sizeof(pkthdr_t));

    pkthdr_t *pkthdr_0 = reinterpret_cast<pkthdr_t *>(buffer.buf);
    pkthdr_0->magic = kPktHdrMagic;

    // UDP checksum for raw Ethernet. Useless for other transports.
    static_assert(sizeof(pkthdr_t::headroom) == kHeadroom + 2, "");
    pkthdr_0->headroom[kHeadroom] = 0;
    pkthdr_0->headroom[kHeadroom + 1] = 0;
  }

  /// Construct a single-packet "fake" MsgBuffer using a received packet,
  /// setting \p buffer to invalid so that we know not to free it.
  /// \p pkt must have space for \p max_data_bytes and one packet header.
  MsgBuffer(pkthdr_t *pkthdr, size_t max_data_size)
      : buf(reinterpret_cast<uint8_t *>(pkthdr) + sizeof(pkthdr_t)),
        max_data_size(max_data_size),
        data_size(max_data_size),
        max_num_pkts(1),
        num_pkts(1) {
    assert(pkthdr->check_magic());  // pkthdr is the zeroth header
    // max_data_size can be zero for control packets, so can't assert

    buffer.buf = nullptr;  // Mark as a non-dynamic ("fake") MsgBuffer
  }

  /// Resize this MsgBuffer to any size smaller than its maximum allocation
  inline void resize(size_t new_data_size, size_t new_num_pkts) {
    assert(new_data_size <= max_data_size);
    assert(new_num_pkts <= max_num_pkts);
    data_size = new_data_size;
    num_pkts = new_num_pkts;
  }

 public:
  /// Pointer to the first *data* byte. (\p buffer.buf does not point to the
  /// first data byte.) The MsgBuffer is invalid if this is null.
  uint8_t *buf;

 private:
  Buffer buffer;  ///< The (optional) backing hugepage Buffer

  // Size info
  size_t max_data_size;  ///< Max data bytes in the MsgBuffer
  size_t data_size;      ///< Current data bytes in the MsgBuffer
  size_t max_num_pkts;   ///< Max number of packets in this MsgBuffer
  size_t num_pkts;       ///< Current number of packets in this MsgBuffer
};
}
