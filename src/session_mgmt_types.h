#ifndef ERPC_SESSION_MGMT_PKT_TYPE_H
#define ERPC_SESSION_MGMT_PKT_TYPE_H

#include <string>
#include "common.h"

namespace ERpc {

/// High-level types of packets used for session management
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
  exit(-1);
  return std::string("");
}

/// Check if a session management packet type is valid
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

/// Check if a valid session management packet type is a request type. Use
/// the complement of this to check if a packet is a response.
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
  exit(-1);
  return false;
}

/// Convert the request session management packet type sm_pkt_type to its
/// corresponding response packet type.
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

/// The types of responses to a session management packet
enum class SessionMgmtErrType : int {
  kNoError,         /* The only non-error error type */
  kTooManySessions, /* Connect req failed because server is out of sessions */
  kInvalidRemoteAppTid,
  kInvalidRemotePort,
  kInvalidTransport
};

static bool session_mgmt_is_valid_err_type(SessionMgmtErrType err_type) {
  switch (err_type) {
    case SessionMgmtErrType::kNoError:
    case SessionMgmtErrType::kTooManySessions:
    case SessionMgmtErrType::kInvalidRemoteAppTid:
    case SessionMgmtErrType::kInvalidRemotePort:
    case SessionMgmtErrType::kInvalidTransport:
      return true;
  }
  return false;
}

static std::string session_mgmt_err_type_str(SessionMgmtErrType err_type) {
  assert(session_mgmt_is_valid_err_type(err_type));

  switch (err_type) {
    case SessionMgmtErrType::kNoError:
      return std::string("[No error]");
    case SessionMgmtErrType::kTooManySessions:
      return std::string("[Too many sessions]");
    case SessionMgmtErrType::kInvalidRemoteAppTid:
      return std::string("[Invalid remote app TID]");
    case SessionMgmtErrType::kInvalidRemotePort:
      return std::string("[Invalid remote port]");
    case SessionMgmtErrType::kInvalidTransport:
      return std::string("[Invalid transport]");
  }
  exit(-1);
  return std::string("");
}
}  // End ERpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H
