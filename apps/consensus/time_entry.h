/**
 * @file time_entry.h
 * @brief A class to record event timestamps with low overhead
 */

#ifndef TIME_ENTRY_H
#define TIME_ENTRY_H

#include <stdlib.h>
#include "rpc.h"

// Comments describe the common-case usage
enum class TimeEntryType {
  kClientReq,   // Client request received by leader
  kSendAeReq,   // Leader sends appendentry request
  kRecvAeReq,   // Follower receives appendentry request
  kSendAeResp,  // Follower sends appendentry response
  kRecvAeResp,  // Leader receives appendentry response
  kCommitted    // Entry committed at leader
};

class TimeEntry {
 public:
  TimeEntryType time_entry_type;
  size_t tsc;

  TimeEntry() {}
  TimeEntry(TimeEntryType t, size_t tsc) : time_entry_type(t), tsc(tsc) {}

  std::string to_string(size_t base_tsc, double freq_ghz) const {
    std::string ret;

    switch (time_entry_type) {
      case TimeEntryType::kClientReq:
        ret = "client_req";
        break;
      case TimeEntryType::kSendAeReq:
        ret = "send_appendentries_req";
        break;
      case TimeEntryType::kRecvAeReq:
        ret = "recv_appendentries_req";
        break;
      case TimeEntryType::kSendAeResp:
        ret = "send_appendentries_resp";
        break;
      case TimeEntryType::kRecvAeResp:
        ret = "recv_appendentries_resp";
        break;
      case TimeEntryType::kCommitted:
        ret = "committed";
        break;
    }

    double usec = ERpc::to_usec(tsc - base_tsc, freq_ghz);
    ret += ": " + std::to_string(usec);
    return ret;
  }
};

#endif  // TIME_ENTRY_H
