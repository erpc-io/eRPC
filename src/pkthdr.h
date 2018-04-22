#ifndef ERPC_PKTHDR_H
#define ERPC_PKTHDR_H

#include "common.h"

namespace erpc {

/// We need a non-zero array size for headroom, even when kHeadroom is zero.
/// We could use (kHeadroom + 1) bytes, but using (kHeadroom + 2) bytes allows
/// space for the two-byte UDP checksum.
static constexpr size_t kHeadroomHackBits = 16;

static constexpr size_t kMsgSizeBits = 24;  ///< Bits for message size
static constexpr size_t kReqNumBits = 44;   ///< Bits for request number
static constexpr size_t kPktNumBits = 14;   ///< Bits for packet number

/// Debug bits for packet header. Also useful for making the total size of all
/// pkthdr_t bitfields equal to 128 bits, which makes copying faster.
static const size_t kPktHdrMagicBits =
    128 -
    (kHeadroomHackBits + 8 + kMsgSizeBits + 16 + 2 + kPktNumBits + kReqNumBits);
static constexpr size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

static_assert(kPktHdrMagicBits == 4, "");  // Just to keep track
static_assert(kPktHdrMagic < (1ull << kPktHdrMagicBits), "");

/// These packet types are stored as bitfields in the packet header, so don't
/// use an enum class here to avoid casting all over the place.
enum PktType : uint64_t {
  kPktTypeReq,         ///< Request data
  kPktTypeReqForResp,  ///< Request for response
  kPktTypeExplCR,      ///< Explicit credit return
  kPktTypeResp,        ///< Response data
};

static std::string pkt_type_str(uint64_t pkt_type) {
  switch (pkt_type) {
    case kPktTypeReq:
      return "request";
    case kPktTypeReqForResp:
      return "request for response";
    case kPktTypeExplCR:
      return "explicit credit return";
    case kPktTypeResp:
      return "response";
  }

  throw std::runtime_error("Invalid packet type.");
}

struct pkthdr_t {
  static_assert(kHeadroom == 0 || kHeadroom == 40, "");
  uint8_t headroom[kHeadroom + 2];   ///< Ethernet L2/L3/L3 headers
  uint64_t req_type : 8;             ///< RPC request type
  uint64_t msg_size : kMsgSizeBits;  ///< Req/resp msg size, excluding headers
  uint64_t dest_session_num : 16;    ///< Destination session number
  uint64_t pkt_type : 2;             ///< The packet type
  uint64_t pkt_num : kPktNumBits;    ///< Monotonically increasing packet number

  /// Request number, carried by all data and control packets for a request.
  uint64_t req_num : kReqNumBits;
  uint64_t magic : kPktHdrMagicBits;  ///< Magic from alloc_msg_buffer()

  /// Fill in packet header fields
  void format(uint64_t _req_type, uint64_t _msg_size,
              uint64_t _dest_session_num, uint64_t _pkt_type, uint64_t _pkt_num,
              uint64_t _req_num) {
    req_type = _req_type;
    msg_size = _msg_size;
    dest_session_num = _dest_session_num;
    pkt_type = _pkt_type;
    pkt_num = _pkt_num;
    req_num = _req_num;
    magic = kPktHdrMagic;
  }

  bool matches(PktType _pkt_type, uint64_t _pkt_num) const {
    return pkt_type == _pkt_type && pkt_num == _pkt_num;
  }

  /// Return a string representation of this packet header
  std::string to_string() const {
    std::ostringstream ret;
    ret << "[type " << pkt_type_str(pkt_type) << ", "
        << "destssn " << std::to_string(dest_session_num) << ", "
        << "reqn " << std::to_string(req_num) << ", "
        << "pktn " << std::to_string(pkt_num) << ", "
        << "msgsz " << std::to_string(msg_size) << ", "
        << "magic " << std::to_string(magic) << "]";

    return ret.str();
  }

  /// Return a pointer to the eRPC header in this packet header
  inline uint8_t *ehdrptr() {
    return reinterpret_cast<uint8_t *>(this) + kHeadroom;
  }

  /// Return a const pointer to the eRPC header in this const packet header
  inline const uint8_t *ehdrptr() const {
    return reinterpret_cast<const uint8_t *>(this) + kHeadroom;
  }

  inline bool check_magic() const { return magic == kPktHdrMagic; }

  inline bool is_req() const { return pkt_type == kPktTypeReq; }
  inline bool is_req_for_resp() const { return pkt_type == kPktTypeReqForResp; }
  inline bool is_resp() const { return pkt_type == kPktTypeResp; }
  inline bool is_expl_cr() const { return pkt_type == kPktTypeExplCR; }

} __attribute__((packed));

static_assert(sizeof(pkthdr_t) % sizeof(size_t) == 0, "");
}

#endif  // ERPC_PKTHDR_H
