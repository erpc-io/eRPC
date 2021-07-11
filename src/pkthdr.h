#pragma once

#include "common.h"
#include "transport_impl/eth_common.h"

namespace erpc {

// Explanation of the headroom hack:
//
// Transports may need different amounts of space for the application-provided
// network header. This "headroom" space is zero for InfiniBand since the driver
// internally creates a header, and 42 bytes for UDP (i.e., L2+L3+L4 headers).
//
// Since we cannot have a zero-length array in C++, the headroom space is
// calculated as (kHeadroom + 2) bytes, where kHeadroom is zero for InfiniBand
// and 40 for UDP. We could have used (kHeadroom + 1) bytes, but (kHeadroom + 2)
// simplifies things by fitting the entire UDP header.
static constexpr size_t kHeadroomHackBits = 16;

static constexpr size_t kMsgSizeBits = 24;  ///< Bits for message size
static constexpr size_t kReqNumBits = 44;   ///< Bits for request number
static constexpr size_t kPktNumBits = 14;   ///< Bits for packet number

/// Debug bits for packet header. Also useful for making the total size of all
/// pkthdr_t bitfields equal to 128 bits, which makes copying faster.
static const size_t k_pkt_hdr_magic_bits =
    128 -
    (kHeadroomHackBits + 8 + kMsgSizeBits + 16 + 2 + kPktNumBits + kReqNumBits);
static constexpr size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

static_assert(k_pkt_hdr_magic_bits == 4, "");  // Just to keep track
static_assert(kPktHdrMagic < (1ull << k_pkt_hdr_magic_bits), "");

/// These packet types are stored as bitfields in the packet header, so don't
/// use an enum class here to avoid casting all over the place.
enum PktType : uint64_t {
  kReq,     ///< Request data
  kRFR,     ///< Request for response
  kExplCR,  ///< Explicit credit return
  kResp,    ///< Response data
};

static std::string pkt_type_str(uint64_t pkt_type) {
  switch (pkt_type) {
    case PktType::kReq: return "REQ";
    case PktType::kRFR: return "RFR";
    case PktType::kExplCR: return "CR";
    case PktType::kResp: return "RESP";
  }

  throw std::runtime_error("Invalid packet type.");
}

struct pkthdr_t {
  static_assert(kHeadroom == 0 || kHeadroom == 40, "");
  uint8_t headroom_[kHeadroom + 2];  /// Ethernet L2/L3/L3 headers

  // Fit one set of fields in six bytes, left over after (kHeadroom + 2) bytes.
  // On MSVC, these fields cannot use uint64_t. In the future, we can increase
  // the bits for msg_size_ by shrinking req_type_.

  uint32_t req_type_ : 8;             /// RPC request type
  uint32_t msg_size_ : kMsgSizeBits;  /// Req/resp msg size, excluding headers
  uint16_t dest_session_num_;  /// Session number of the destination endpoint

  // The next set of fields goes in eight bytes total

  uint64_t pkt_type_ : 2;           /// The eRPC packet type
  uint64_t pkt_num_ : kPktNumBits;  /// Monotonically increasing packet number

  /// Request number, carried by all data and control packets for a request.
  uint64_t req_num_ : kReqNumBits;
  uint64_t magic_ : k_pkt_hdr_magic_bits;  ///< Magic from alloc_msg_buffer()

  /// Fill in packet header fields
  void format(uint64_t _req_type, uint64_t _msg_size,
              uint64_t _dest_session_num, uint64_t _pkt_type, uint64_t _pkt_num,
              uint64_t _req_num) {
    req_type_ = _req_type;
    msg_size_ = _msg_size;
    dest_session_num_ = _dest_session_num;
    pkt_type_ = _pkt_type;
    pkt_num_ = _pkt_num;
    req_num_ = _req_num;
    magic_ = kPktHdrMagic;
  }

  bool matches(PktType _pkt_type, uint64_t _pkt_num) const {
    return pkt_type_ == _pkt_type && pkt_num_ == _pkt_num;
  }

  /// Return a string with the eRPC header fields in this packet header
  std::string to_string() const {
    std::ostringstream ret;
    ret << "[type " << pkt_type_str(pkt_type_) << ", "
        << "dsn " << std::to_string(dest_session_num_) << ", "
        << "reqn " << std::to_string(req_num_) << ", "
        << "pktn " << std::to_string(pkt_num_) << ", "
        << "msz " << std::to_string(msg_size_) << ", "
        << "magic " << std::to_string(magic_) << "]";

    return ret.str();
  }

  /// Return a string representation of this packet's headroom, which contains
  /// the Ethernet packet header for Ethernet-based transports.
  std::string headroom_string() const {
    if (kHeadroom == 0) return "[Not available for IB]";
    return frame_header_to_string(&headroom_[0]);
  }

  /// Return a pointer to the eRPC header in this packet header
  inline uint8_t *ehdrptr() {
    return reinterpret_cast<uint8_t *>(this) + kHeadroom;
  }

  /// Get the Ethernet header from this packet header
  inline eth_hdr_t *get_eth_hdr() {
    assert(kHeadroom == 40);
    return reinterpret_cast<eth_hdr_t *>(&this->headroom_[0]);
  }

  /// Get the IPv4 header from this packet header
  inline ipv4_hdr_t *get_ipv4_hdr() {
    assert(kHeadroom == 40);
    return reinterpret_cast<ipv4_hdr_t *>(&this->headroom_[0] +
                                          sizeof(eth_hdr_t));
  }

  /// Get the UDP header from this packet header
  inline udp_hdr_t *get_udp_hdr() {
    assert(kHeadroom == 40);
    return reinterpret_cast<udp_hdr_t *>(
        &this->headroom_[0] + sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t));
  }

  /// Return a const pointer to the eRPC header in this const packet header
  inline const uint8_t *ehdrptr() const {
    return reinterpret_cast<const uint8_t *>(this) + kHeadroom;
  }

  inline bool check_magic() const { return magic_ == kPktHdrMagic; }

  inline bool is_req() const { return pkt_type_ == PktType::kReq; }
  inline bool is_rfr() const { return pkt_type_ == PktType::kRFR; }
  inline bool is_resp() const { return pkt_type_ == PktType::kResp; }
  inline bool is_expl_cr() const { return pkt_type_ == PktType::kExplCR; }

} __attribute__((packed));

static_assert(sizeof(pkthdr_t) == kHeadroom + 16, "");
static_assert(sizeof(pkthdr_t) % sizeof(size_t) == 0, "");

}  // namespace erpc
