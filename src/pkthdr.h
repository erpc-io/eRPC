#ifndef ERPC_PKTHDR_H
#define ERPC_PKTHDR_H

#include "common.h"

namespace erpc {

// Packet header
static constexpr size_t kMsgSizeBits = 24;  ///< Bits for message size
static constexpr size_t kReqNumBits = 44;   ///< Bits for request number
static constexpr size_t kPktNumBits = 13;   ///< Bits for packet number

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
  uint64_t req_type : 8;             ///< RPC request type
  uint64_t msg_size : kMsgSizeBits;  ///< Req/resp msg size, excluding headers
  uint64_t dest_session_num : 16;    ///< Session number of the destination
  uint64_t pkt_type : 2;             ///< The packet type
  /// Packet number. For an explicit credit return packet, this is equal to the
  /// packet number of the corresponding request packet. For a
  /// request-for-response packet, this is equal to the packet number of the
  /// corresponding response packet.
  uint64_t pkt_num : kPktNumBits;
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
        << "dest session " << std::to_string(dest_session_num) << ", "
        << "req " << std::to_string(req_num) << ", "
        << "pkt " << std::to_string(pkt_num) << ", "
        << "msg size " << std::to_string(msg_size) << "]";

    return ret.str();
  }

  /// Return a string representation of a packet header, with custom pkt_num.
  std::string to_string(size_t _pkt_num) const {
    std::ostringstream ret;
    ret << "[type " << pkt_type_str(pkt_type) << ", "
        << "dest session " << std::to_string(dest_session_num) << ", "
        << "req " << std::to_string(req_num) << ", "
        << "pkt " << std::to_string(_pkt_num) << ", "
        << "msg size " << std::to_string(msg_size) << "]";

    return ret.str();
  }

  inline bool check_magic() const { return magic == kPktHdrMagic; }

  inline bool is_req() const { return pkt_type == kPktTypeReq; }
  inline bool is_req_for_resp() const { return pkt_type == kPktTypeReqForResp; }
  inline bool is_resp() const { return pkt_type == kPktTypeResp; }
  inline bool is_expl_cr() const { return pkt_type == kPktTypeExplCR; }

} __attribute__((packed));

// pkthdr_t size should be a power of 2 for cheap multiplication
static_assert(sizeof(pkthdr_t) == 16, "");
}

#endif  // ERPC_PKTHDR_H
