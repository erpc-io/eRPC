/**
 * @file client.h
 * @brief The client for the replicated service
 */

#include "consensus.h"
#include "util/latency.h"

#ifndef CLIENT_H
#define CLIENT_H

// Request tag, which doubles up as the request descriptor for the request queue
union client_req_tag_t {
  struct {
    uint64_t session_idx : 32;  // Index into context's session_num array
    uint64_t msgbuf_idx : 32;   // Index into context's req_msgbuf array
  };

  size_t _tag;

  client_req_tag_t(uint64_t session_idx, uint64_t msgbuf_idx)
      : session_idx(session_idx), msgbuf_idx(msgbuf_idx) {}
  client_req_tag_t(size_t _tag) : _tag(_tag) {}
  client_req_tag_t() : _tag(0) {}
};
static_assert(sizeof(client_req_tag_t) == sizeof(size_t), "");

void client_func(size_t, ERpc::Nexus<ERpc::IBTransport> *) {}

#endif
