#ifndef ERPC_PKTHDR_H
#define ERPC_PKTHDR_H

#include <sstream>
#include "common.h"

namespace ERpc {

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
    ret << "[type " << pkt_type_str(pkt_type) << ", "
        << "req " << req_num << ", "
        << "pkt " << pkt_num << ", "
        << "msg size " << msg_size << "]"
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
}

#endif  // ERPC_PKTHDR_H
