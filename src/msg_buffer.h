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

  /// Construct a MsgBuffer with a valid Buffer allocated by eRPC.
  /// The zeroth packet header is stored at \p buffer.buf. \p buffer must have
  /// space for at least \p data_bytes, and \p num_pkts packet headers.
  MsgBuffer(Buffer buffer, size_t data_size, size_t num_pkts)
      : buffer(buffer), data_size(data_size), num_pkts(num_pkts) {
    assert(buffer.buf != nullptr); /* buffer must be valid */
    /* data_size can be 0 */
    assert(num_pkts >= 1);
    assert(buffer.class_size >= data_size + num_pkts * sizeof(pkthdr_t));
    buf = buffer.buf + sizeof(pkthdr_t);
  }

  /// Construct a single-packet MsgBuffer using an arbitrary chunk of memory.
  /// \p buf must have space for \p data_bytes and one packet header.
  MsgBuffer(uint8_t *buf, size_t data_size)
      : buf(buf + sizeof(pkthdr_t)),
        buffer(Buffer::get_invalid_buffer()),
        data_size(data_size),
        num_pkts(1) {
    assert(buf != nullptr);
    /* data_size can be zero */
  }

  /// Default constructor. The \p buf field is NULL, indicating invalid state.
  MsgBuffer() {}

  /// Return an invalid MsgBuffer, i.e., \p buf is NULL.
  static MsgBuffer get_invalid_msgbuf() { return MsgBuffer(); }

  ~MsgBuffer() {}

  /// Return a pointer to the pre-appended packet header of this MsgBuffer
  inline pkthdr_t *get_pkthdr_0() {
    return (pkthdr_t *)(buf - sizeof(pkthdr_t));
  }

  /// Return a pointer to the nth (n >= 1) packet header of this MsgBuffer
  inline pkthdr_t *get_pkthdr_n(size_t n) {
    assert(n >= 1);
    return (pkthdr_t *)(buf + round_up<sizeof(size_t)>(data_size) +
                        (n - 1) * sizeof(pkthdr_t));
  }

  /// Check if a MsgBuffer's header magic is valid
  inline bool check_pkthdr_0() {
    return (get_pkthdr_0()->magic == kPktHdrMagic);
  }

  uint8_t *buf = nullptr;
  /// Pointer to the first *data* byte. (\p buffer.buf does not point to the
  /// first data byte.)
 private:
  Buffer buffer;         ///< The (optional) backing hugepage Buffer
  size_t data_size = 0;  ///< Total data bytes in the MsgBuffer
  size_t num_pkts = 0;   ///< Total number of packets in this message
  size_t data_sent = 0;  ///< Bytes of data already sent
  union {
    size_t pkts_sent = 0;  ///< Packets already sent (for tx MsgBuffers)
    size_t pkts_rcvd;      ///< Packets already received (for rx MsgBuffers)
  };
};
}

#endif  // ERPC_MSG_BUFFER_H
