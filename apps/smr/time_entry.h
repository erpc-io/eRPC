/**
 * @file time_entry.h
 * @brief A class to record event timestamps with low overhead
 */

#pragma once

#include <stdlib.h>
#include "rpc.h"

// Comments describe the common-case usage
enum class TimeEntType {
  kClientReq,   // Client request received by leader
  kSendAeReq,   // Leader sends appendentry request
  kRecvAeReq,   // Follower receives appendentry request
  kSendAeResp,  // Follower sends appendentry response
  kRecvAeResp,  // Leader receives appendentry response
  kCommitted    // Entry committed at leader
};

class TimeEnt {
 public:
  TimeEntType type;
  size_t tsc;
  TimeEnt(TimeEntType t) : type(t), tsc(erpc::rdtsc()) {}

  std::string to_string(size_t base_tsc, double freq_ghz) const {
    std::string ret;

    switch (type) {
      case TimeEntType::kClientReq: ret = "client_req"; break;
      case TimeEntType::kSendAeReq: ret = "send_appendentries_req"; break;
      case TimeEntType::kRecvAeReq: ret = "recv_appendentries_req"; break;
      case TimeEntType::kSendAeResp: ret = "send_appendentries_resp"; break;
      case TimeEntType::kRecvAeResp: ret = "recv_appendentries_resp"; break;
      case TimeEntType::kCommitted: ret = "committed"; break;
    }

    double usec = erpc::to_usec(tsc - base_tsc, freq_ghz);
    ret += ": " + std::to_string(usec);
    return ret;
  }
};
