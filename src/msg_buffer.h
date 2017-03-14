#ifndef ERPC_MSG_BUFFER_H
#define ERPC_MSG_BUFFER_H

#include "common.h"
#include "pkthdr.h"
#include "util/buffer.h"

namespace ERpc {

// Forward declarations
class IBTransport;
template <typename T>
class Rpc;

/// A message buffer with headers at the beginning and end. A MsgBuffer is
/// invalid if its \p buf field is NULL.
class MsgBuffer {
 public:
  friend class IBTransport;
  friend class Rpc<IBTransport>;

  /// Default constructor. The \p buf field is NULL, indicating invalid state.
  MsgBuffer() {}
  ~MsgBuffer() {}

  /// Return an invalid MsgBuffer, i.e., \p buf is NULL.
  static MsgBuffer get_invalid_msgbuf() { return MsgBuffer(); }

  /// Return a pointer to the pre-appended packet header of this MsgBuffer
  inline pkthdr_t *get_pkthdr_0() const {
    return (pkthdr_t *)(buf - sizeof(pkthdr_t));
  }

  /// Return a pointer to the nth (n >= 1) packet header of this MsgBuffer.
  /// This must use \p max_data_size, not \p data_size.
  inline pkthdr_t *get_pkthdr_n(size_t n) const {
    assert(n >= 1);
    return (pkthdr_t *)(buf + round_up<sizeof(size_t)>(max_data_size) +
                        (n - 1) * sizeof(pkthdr_t));
  }

  /// Check if a MsgBuffer's header magic is valid
  inline bool check_pkthdr_0() const {
    return (get_pkthdr_0()->magic == kPktHdrMagic);
  }

 private:
  /// Construct a MsgBuffer with a valid Buffer allocated by eRPC.
  /// The zeroth packet header is stored at \p buffer.buf. \p buffer must have
  /// space for at least \p max_data_bytes, and \p max_num_pkts packet headers.
  MsgBuffer(Buffer buffer, size_t max_data_size, size_t max_num_pkts)
      : buf(buffer.buf + sizeof(pkthdr_t)),
        buffer(buffer),
        max_data_size(max_data_size),
        data_size(max_data_size),
        max_num_pkts(max_num_pkts),
        num_pkts(max_num_pkts) {
    assert(buffer.buf != nullptr); /* buffer must be valid */
    /* data_size can be 0 */
    assert(max_num_pkts >= 1);
    assert(buffer.class_size >=
           max_data_size + max_num_pkts * sizeof(pkthdr_t));
  }

  /// Construct a single-packet MsgBuffer using an arbitrary chunk of memory.
  /// \p buf must have space for \p max_data_bytes and one packet header.
  MsgBuffer(uint8_t *buf, size_t max_data_size)
      : buf(buf + sizeof(pkthdr_t)),
        buffer(Buffer::get_invalid_buffer()),
        max_data_size(max_data_size),
        data_size(max_data_size),
        max_num_pkts(1),
        num_pkts(1) {
    assert(buf != nullptr);
    /* data_size can be zero */
  }

  inline void resize(size_t new_data_size, size_t new_num_pkts) {
    assert(new_data_size <= max_data_size);
    assert(new_num_pkts <= max_num_pkts);
    data_size = new_data_size;
    num_pkts = new_num_pkts;
  }

 public:
  /// Pointer to the first *data* byte. (\p buffer.buf does not point to the
  /// first data byte.)
  uint8_t *buf = nullptr;

 private:
  Buffer buffer;  ///< The (optional) backing hugepage Buffer

  // Size info
  size_t max_data_size = 0;  ///< Max data bytes in the MsgBuffer
  size_t data_size = 0;      ///< Current data bytes in the MsgBuffer
  size_t max_num_pkts = 0;   ///< Max number of packets in this MsgBuffer
  size_t num_pkts = 0;       ///< Current number of packets in this MsgBuffer

  // Progress tracking info
  size_t data_queued = 0;  ///< Bytes of data queued for tx_burst
  union {
    size_t pkts_queued = 0;  ///< Packets queued for tx_burst
    size_t pkts_rcvd;        ///< Packets received from rx_burst
  };
};
}

#endif  // ERPC_MSG_BUFFER_H
