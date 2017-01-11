#ifndef ERPC_SESSION_MGMT_PKT_TYPE_H
#define ERPC_SESSION_MGMT_PKT_TYPE_H

#include <string>
#include "common.h"

namespace ERpc {
/**
 * @brief High-level types of packets used for session management
 */
enum class SessionMgmtPktType : int {
  kConnectReq,
  kConnectResp,
  kDisconnectReq,
  kDisconnectResp
};

static std::string session_mgmt_pkt_type_str(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
      return std::string("[Connect request]");
    case SessionMgmtPktType::kConnectResp:
      return std::string("[Connect response]");
    case SessionMgmtPktType::kDisconnectReq:
      return std::string("[Disconnect request]");
    case SessionMgmtPktType::kDisconnectResp:
      return std::string("[Disconnect response]");
  };
  return std::string("[Invalid type]");
}

/**
 * @brief Check if a session management packet type is valid
 */
static bool is_valid_session_mgmt_pkt_type(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectReq:
    case SessionMgmtPktType::kDisconnectResp:
      return true;
  }
  return false;
}

/**
 * @brief Check if a valid session management packet type is a request type. Use
 * the complement of this to check if a packet is a response.
 */
static bool is_session_mgmt_pkt_type_req(SessionMgmtPktType sm_pkt_type) {
  assert(is_valid_session_mgmt_pkt_type(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kDisconnectReq:
      return true;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      return false;
  }
}

/**
 * @brief Convert the request session management packet type \p sm_pkt_type to
 * its corresponding response packet type.
 */
static SessionMgmtPktType session_mgmt_pkt_type_req_to_resp(
    SessionMgmtPktType sm_pkt_type) {
  assert(is_session_mgmt_pkt_type_req(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
      return SessionMgmtPktType::kConnectResp;
    case SessionMgmtPktType::kDisconnectReq:
      return SessionMgmtPktType::kDisconnectResp;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      exit(-1);
      return static_cast<SessionMgmtPktType>(-1);
  }
}

/**
 * @brief The types of responses to a session management packet
 */
enum class SessionMgmtResponseType : int {
  kConnectSuccess,
  kDisconnectSuccess,
  kSessionExists,
  kInvalidRemoteAppTid,
  kInvalidRemotePort,
};

}  // End ERpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H
