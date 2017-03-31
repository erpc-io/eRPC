#ifndef ERPC_PKTHDR_H
#define ERPC_PKTHDR_H

#include "common.h"

namespace ERpc {

// Packet header
static constexpr size_t kMaxReqTypes = std::numeric_limits<uint8_t>::max();
static constexpr size_t kInvalidReqType = (kMaxReqTypes - 1);
static constexpr size_t kMsgSizeBits = 24;  ///< Bits for message size
static constexpr size_t kReqNumBits = 44;   ///< Bits for request number
static constexpr size_t kInvalidReqNum = ((1ull << kReqNumBits) - 1);
static constexpr size_t kPktNumBits = 13;  ///< Bits for packet number

/// Debug bits for packet header. Also useful for making the total size of all
/// pkthdr_t bitfields equal to 128 bits, which makes copying faster.
static const size_t kPktHdrMagicBits =
    128 - (8 + kMsgSizeBits + 16 + 2 + 1 + 1 + kPktNumBits + kReqNumBits);
static const size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

static_assert(kPktHdrMagicBits == 19, "");  // Just to keep track
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
  uint64_t dest_session_num : 16;    ///< Session number of the destination
  uint64_t pkt_type : 2;             ///< The packet type
  /// 1 if this packet is unexpected. This can be computed using other fields,
  /// but it's useful to have it separately.
  uint64_t is_unexp : 1;
  /// 1 if this packet is a response, and the req func was foreground terminal
  uint64_t fgt_resp : 1;
  uint64_t pkt_num : kPktNumBits;     ///< Packet number in the request
  uint64_t req_num : kReqNumBits;     ///< Request number of this packet
  uint64_t magic : kPktHdrMagicBits;  ///< Magic from alloc_msg_buffer()

  /// Return a string representation of a packet header. Implicit and explicit
  /// credit returns are marked with a + sign.
  std::string to_string() const {
    std::ostringstream ret;

    ret << "[type " << pkt_type_str(pkt_type) << ", "
        << "dest session " << dest_session_num << ", "
        << "req " << req_num << ", "
        << "pkt " << pkt_num << ", "
        << "fgt_resp " << fgt_resp << ", "
        << "msg size " << msg_size << "]" << (is_unexp == 0 ? "+" : "");

    return ret.str();
  }

  inline bool check_magic() const { return magic == kPktHdrMagic; }
  inline bool is_req() const { return pkt_type == kPktTypeReq; }
  inline bool is_resp() const { return pkt_type == kPktTypeResp; }

  inline bool is_credit_return() const {
    return pkt_type == kPktTypeCreditReturn;
  }

} __attribute__((packed));

// pkthdr_t size should be a power of 2 for cheap multiplication
static_assert(sizeof(pkthdr_t) == 16, "");
}

#endif  // ERPC_PKTHDR_H
