#ifndef ERPC_MSG_BUFFER_H
#define ERPC_MSG_BUFFER_H

#include "common.h"
#include "pkthdr.h"
#include "util/buffer.h"
#include "util/math_utils.h"

namespace ERpc {

// Forward declarations for friendship
class IBTransport;
class Session;

template <typename T>
class Rpc;

/// A message buffer with magic-ful headers at the beginning and end
class MsgBuffer {
  friend class IBTransport;
  friend class Session;
  friend class Rpc<IBTransport>;

 public:
  MsgBuffer() {}
  ~MsgBuffer() {}

  /// Return a pointer to the pre-appended packet header of this MsgBuffer
  inline pkthdr_t *get_pkthdr_0() const {
    return reinterpret_cast<pkthdr_t *>(buf - sizeof(pkthdr_t));
  }

  /// Return a pointer to the nth (n >= 1) packet header of this MsgBuffer.
  /// This must use \p max_data_size, not \p data_size.
  inline pkthdr_t *get_pkthdr_n(size_t n) const {
    assert(n >= 1);
    return reinterpret_cast<pkthdr_t *>(
        buf + round_up<sizeof(size_t)>(max_data_size) +
        (n - 1) * sizeof(pkthdr_t));
  }

  ///@{ Accessors for the packet header
  inline bool is_req() const { return get_pkthdr_0()->is_req(); }
  inline bool is_resp() const { return get_pkthdr_0()->is_resp(); }
  inline bool is_expl_cr() const { return get_pkthdr_0()->is_expl_cr(); }
  inline bool is_req_for_resp() const {
    return get_pkthdr_0()->is_req_for_resp();
  }
  inline uint64_t get_req_num() const { return get_pkthdr_0()->req_num; }
  inline uint64_t get_pkt_type() const { return get_pkthdr_0()->pkt_type; }
  inline uint8_t get_req_type() const { return get_pkthdr_0()->req_type; }
  ///@}

  /// Check if a MsgBuffer's magic is valid
  inline bool check_magic() const {
    if (likely(get_pkthdr_0()->magic == kPktHdrMagic)) {
      return true;
    }
    return false;
  }

  /// Check if this MsgBuffer uses dynamic memory allocation
  inline bool is_dynamic() const { return buffer.buf != nullptr; }

  /// Used by applications to get the current data size of a MsgBuffer
  inline size_t get_data_size() const { return data_size; }

  /// Return a string representation of this MsgBuffer
  std::string to_string() const {
    if (buf == nullptr) {
      return "[Invalid]";
    }

    std::ostringstream ret;
    ret << "[buf " << static_cast<void *>(buf) << ", "
        << "buffer " << buffer.to_string() << ", "
        << "data " << data_size << "(" << max_data_size << "), "
        << "pkts " << num_pkts << "(" << max_num_pkts << "), "
        << "pkts queued/rcvd " << pkts_queued << "]";
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
  }

  /// Construct a single-packet MsgBuffer using a received packet, setting
  /// buffer to invalid so that we know not to free it.
  /// \p pkt must have space for \p max_data_bytes and one packet header.
  MsgBuffer(const uint8_t *pkt, size_t max_data_size)
      : buf(const_cast<uint8_t *>(pkt) + sizeof(pkthdr_t)),
        max_data_size(max_data_size),
        data_size(max_data_size),
        max_num_pkts(1),
        num_pkts(1) {
    assert(buf != nullptr);
    // max_data_size can be zero
    buffer.buf = nullptr;  // This is a non-dynamic ("fake") MsgBuffer

    const pkthdr_t *pkthdr_0 = reinterpret_cast<const pkthdr_t *>(pkt);
    _unused(pkthdr_0);
    assert(pkthdr_0->check_magic());
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
  /// first data byte.) The MsgBuffer is invalid if this is NULL.
  uint8_t *buf;

 private:
  Buffer buffer;  ///< The (optional) backing hugepage Buffer

  // Size info
  size_t max_data_size;  ///< Max data bytes in the MsgBuffer
  size_t data_size;      ///< Current data bytes in the MsgBuffer
  size_t max_num_pkts;   ///< Max number of packets in this MsgBuffer
  size_t num_pkts;       ///< Current number of packets in this MsgBuffer

  // Progress tracking info
  union {
    size_t pkts_queued;  ///< Packets queued for tx_burst
    size_t pkts_rcvd;    ///< Packets received from rx_burst
  };
};
}

#endif  // ERPC_MSG_BUFFER_H
