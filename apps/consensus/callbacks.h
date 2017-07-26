/**
 * @file callbacks.h
 * Simple callbacks for Raft. Log persistence callbacks are ignored since we
 * don't maintain an application-level persisent log. Note that internally,
 * Raft maintains an in-memory log.
 */

#ifndef CALLBACKS_H
#define CALLBACKS_H

#include "consensus.h"

// Raft callback for applying an entry to the finite state machine
static int __raft_applylog(raft_server_t *, void *, raft_entry_t *ety, int) {
  assert(!raft_entry_is_cfg_change(ety));
  assert(ety->data.len == sizeof(int));

  unsigned int *ticket = static_cast<unsigned int *>(ety->data.buf);
  if (kAppVerbose) {
    printf("consensus: Adding ticket %d [%s].\n", *ticket,
           ERpc::get_formatted_time().c_str());
  }

  sv->tickets.insert(*ticket);
  return 0;
}

// Raft callback for saving voted_for field to persistent storage.
static int __raft_persist_vote(raft_server_t *, void *, const int) {
  return 0;  // Ignored
}

// Raft callback for saving term field to persistent storage
static int __raft_persist_term(raft_server_t *, void *, const int) {
  return 0;  // Ignored
}

// Raft callback for appending an item to the log
static int __raft_logentry_offer(raft_server_t *, void *, raft_entry_t *ety,
                                 int) {
  assert(!raft_entry_is_cfg_change(ety));
  return 0;  // Ignored
}

// Raft callback for deleting the most recent entry from the log. This happens
// when an invalid leader finds a valid leader and has to delete superseded
// log entries.
static int __raft_logentry_pop(raft_server_t *, void *, raft_entry_t *, int) {
  return 0;  // Ignored
}

// Raft callback for removing the first entry from the log. This is provided to
// support log compaction in the future.
static int __raft_logentry_poll(raft_server_t *, void *, raft_entry_t *, int) {
  return 0;  // Ignored
}

// Non-voting node now has enough logs to be able to vote. Append a finalization
// cfg log entry.
static int __raft_node_has_sufficient_logs(raft_server_t *, void *,
                                           raft_node_t *) {
  assert(false);  // Ignored
  return 0;
}

// Raft callback for displaying debugging information
void __raft_log(raft_server_t *, raft_node_t *, void *, const char *buf) {
  printf("raft: %s [%s].\n", buf, ERpc::get_formatted_time().c_str());
}

#endif
