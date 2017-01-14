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
static bool session_mgmt_is_valid_pkt_type(SessionMgmtPktType sm_pkt_type) {
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
static bool session_mgmt_is_pkt_type_req(SessionMgmtPktType sm_pkt_type) {
  assert(session_mgmt_is_valid_pkt_type(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kDisconnectReq:
      return true;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      return false;
  }
  return false;
}

/**
 * @brief Convert the request session management packet type \p sm_pkt_type to
 * its corresponding response packet type.
 */
static SessionMgmtPktType session_mgmt_pkt_type_req_to_resp(
    SessionMgmtPktType sm_pkt_type) {
  assert(session_mgmt_is_pkt_type_req(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
      return SessionMgmtPktType::kConnectResp;
    case SessionMgmtPktType::kDisconnectReq:
      return SessionMgmtPktType::kDisconnectResp;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      break;
  }

  exit(-1);
  return static_cast<SessionMgmtPktType>(-1);
}

/**
 * @brief The types of responses to a session management packet
 */
enum class SessionMgmtRespType : int {
  kConnectSuccess,    /* The connect req succeeded */
  kDisconnectSuccess, /* The disconnect req succeeded */

  // Errors
  kTooManySessions, /* Connect req failed because server is out of sessions */
  kInvalidRemoteAppTid,
  kInvalidRemotePort,
  kInvalidTransport
};

static std::string session_mgmt_resp_type_str(SessionMgmtRespType resp_type) {
  switch (resp_type) {
    case SessionMgmtRespType::kConnectSuccess:
      return std::string("[Connect success]");
    case SessionMgmtRespType::kDisconnectSuccess:
      return std::string("[Disconnect success]");
    case SessionMgmtRespType::kTooManySessions:
      return std::string("[Too many sessions]");
    case SessionMgmtRespType::kInvalidRemoteAppTid:
      return std::string("[Invalid remote app TID]");
    case SessionMgmtRespType::kInvalidRemotePort:
      return std::string("[Invalid remote port]");
    case SessionMgmtRespType::kInvalidTransport:
      return std::string("[Invalid transport]");
  }
}

/**
 * @brief Return true iff \p resp_type is an error response type
 */
static bool session_mgmt_is_resp_type_err(SessionMgmtRespType resp_type) {
  switch (resp_type) {
    case SessionMgmtRespType::kConnectSuccess:
    case SessionMgmtRespType::kDisconnectSuccess:
      return false;
    case SessionMgmtRespType::kTooManySessions:
    case SessionMgmtRespType::kInvalidRemoteAppTid:
    case SessionMgmtRespType::kInvalidRemotePort:
    case SessionMgmtRespType::kInvalidTransport:
      return true;
  }
}

}  // End ERpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H
