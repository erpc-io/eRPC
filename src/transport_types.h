/**
 * @file transport_types.h
 *
 * @brief Generic definitions required to support multiple fabrics.
 *
 * This stuff cannot go in transport.h. Several classes (e.g., Session and
 * HugeAlloc) require these generic definitions, and these classes are in turn
 * required by the Transport class.
 */

#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>
#include <functional>
#include <sstream>
#include <string>
#include "common.h"
#include "util/buffer.h"

namespace ERpc {

static const size_t kMaxRoutingInfoSize = 128;  ///< Space for routing info
static const size_t kMaxMemRegInfoSize = 64;  ///< Space for memory registration

// Packet header
static const size_t kMsgSizeBits = 24;  ///< Bits for message size
static const size_t kPktNumBits = 13;   ///< Bits for packet number in request
static const size_t kReqNumBits = 44;   ///< Bits for request number

/// Debug bits for packet header. Also useful for sizing pkthdr_t to 128 bits.
static const size_t kPktHdrMagicBits =
    128 - (8 + kMsgSizeBits + 16 + 2 + 1 + kPktNumBits + kReqNumBits);
static const size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

static_assert(kPktHdrMagicBits == 20, ""); /* Just to keep track */
static_assert(kPktHdrMagic < (1ull << kPktHdrMagicBits), "");

/// These packet types are stored as bitfields in the packet header, so don't
/// use an enum class here to avoid casting all over the place.
enum PktType : uint64_t {
  kPktTypeReq,   ///< An Rpc request packet
  kPktTypeResp,  ///< An Rpc response packet
  /// An *explicit* credit return packet. The first response packet is also
  /// a credit return.
  kPktTypeCreditReturn
};

static std::string pkt_type_str(uint64_t pkt_type) {
  switch (pkt_type) {
    case kPktTypeReq:
      return std::string("request");
    case kPktTypeResp:
      return std::string("response");
    case kPktTypeCreditReturn:
      return std::string("credit return");
    default:
      break;
  }

  assert(false);
  exit(-1);
  return std::string("");
}

struct pkthdr_t {
  uint64_t req_type : 8;             ///< RPC request type
  uint64_t msg_size : kMsgSizeBits;  ///< Req/resp msg size, excluding headers
  uint64_t rem_session_num : 16;     ///< Session number of the remote session
  uint64_t pkt_type : 2;             ///< The packet type
  /// 1 if this packet is unexpected. This can be computed using the packet type
  /// and pkt_num, but it's useful to have it separately.
  uint64_t is_unexp : 1;
  uint64_t pkt_num : kPktNumBits;     ///< Packet number in the request
  uint64_t req_num : kReqNumBits;     ///< Request number of this packet
  uint64_t magic : kPktHdrMagicBits;  ///< Magic from alloc_msg_buffer()

  /// Return a string representation of a packet header. Credit return packets
  /// are marked with an asterisk.
  std::string to_string() {
    std::ostringstream ret;
    ret << "[Req " << req_num << ", "
        << "pkt " << pkt_num << ", "
        << "msg size " << msg_size << ", "
        << "pkt type " << pkt_type_str(pkt_type) << "]"
        << (is_unexp == 0 ? "*" : ""); /* Mark credit return packets */
    return ret.str();
  }
} __attribute__((packed));

static_assert(sizeof(pkthdr_t) == 16, "");
/* Cover all the bitfields to make copying cheaper */
static_assert(8 + kMsgSizeBits + 16 + 2 + 1 + kPktNumBits + kReqNumBits +
                      kPktHdrMagicBits ==
                  128,
              "");

/// A message buffer with headers at the beginning and end
class MsgBuffer {
 public:
  /// Construct a MsgBuffer with a Buffer allocated by eRPC. The zeroth packet
  /// header is stored at \p buffer.buf. \p buffer must have space for
  /// \p data_bytes, and \p num_pkts packet headers.
  MsgBuffer(Buffer buffer, size_t data_size, size_t num_pkts)
      : buffer(buffer), data_size(data_size), num_pkts(num_pkts) {
    assert(buffer.class_size >= data_size + num_pkts * sizeof(pkthdr_t));
    buf = buffer.buf + sizeof(pkthdr_t);
  }

  /// Construct a single-packet MsgBuffer using an arbitrary chunk of memory.
  /// \p buf must have space for \p data_bytes and one packet header.
  MsgBuffer(uint8_t *buf, size_t data_size)
      : buffer(Buffer::get_invalid_buffer()),
        buf(buf + sizeof(pkthdr_t)),
        data_size(data_size),
        num_pkts(1) {}

  MsgBuffer() : buffer(Buffer::get_invalid_buffer()) {}

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

  Buffer buffer;  ///< The (optional) backing hugepage Buffer
  /// Pointer to the first *data* byte. (\p buffer.buf does not point to the
  /// first data byte.) A MsgBuffer is invalid if this is NULL.
  uint8_t *buf = nullptr;
  size_t data_size = 0;  ///< Total data bytes in the MsgBuffer
  size_t num_pkts = 0;   ///< Total number of packets in this message
  size_t data_sent = 0;  ///< Bytes of data already sent
  union {
    size_t pkts_sent = 0;  ///< Packets already sent (for tx MsgBuffers)
    size_t pkts_rcvd;      ///< Packets already received (for rx MsgBuffers)
  };
};

/// Generic struct to store routing info for any transport.
struct RoutingInfo {
  uint8_t buf[kMaxRoutingInfoSize];
};

/// Generic struct to store memory registration info for any transport.
struct MemRegInfo {
  void *transport_mr;  ///< The transport-specific memory region (e.g., ibv_mr)
  uint32_t lkey;       ///< The lkey of the memory region

  MemRegInfo(void *transport_mr, uint32_t lkey)
      : transport_mr(transport_mr), lkey(lkey) {}
};

/// Generic types for memory registration and deregistration functions.
typedef std::function<MemRegInfo(void *, size_t)> reg_mr_func_t;
typedef std::function<void(MemRegInfo)> dereg_mr_func_t;

enum class TransportType { kInfiniBand, kRoCE, kOmniPath, kInvalidTransport };

static std::string get_transport_name(TransportType transport_type) {
  switch (transport_type) {
    case TransportType::kInfiniBand:
      return std::string("[InfiniBand]");
    case TransportType::kRoCE:
      return std::string("[RoCE]");
    case TransportType::kOmniPath:
      return std::string("[OmniPath]");
    default:
      return std::string("[Invalid transport]");
  }
}
}  // End ERpc

#endif  // ERPC_TRANSPORT_TYPE_H
