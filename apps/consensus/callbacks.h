/**
 * @file callbacks.h
 * Simple callbacks for Raft. Log persistence callbacks are ignored since we
 * don't maintain an application-level persisent log. Note that internally,
 * Raft maintains an in-memory log.
 */

#ifndef CALLBACKS_H
#define CALLBACKS_H

#include "appendentries.h"
#include "consensus.h"
#include "requestvote.h"

// Raft callback for applying an entry to the finite state machine
static int __raft_applylog(raft_server_t *, void *udata, raft_entry_t *ety,
                           int) {
  assert(udata != nullptr && ety != nullptr);
  assert(!raft_entry_is_cfg_change(ety));

  // We're applying an entry to the application's state machine, so we're sure
  // about its length. Other log callbacks can be invoked for non-application
  // log entries.
  assert(ety->data.len == sizeof(size_t));

  auto *c = static_cast<AppContext *>(udata);
  assert(c->check_magic());

  size_t *counter = static_cast<size_t *>(ety->data.buf);
  ERpc::rt_assert(*counter == c->server.cur_counter + 1, "Invalid counter");
  if (kAppVerbose) {
    printf("consensus: Updating counter to %zu [%s].\n", *counter,
           ERpc::get_formatted_time().c_str());
  }
  c->server.cur_counter++;

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
static int __raft_logentry_offer(raft_server_t *, void *udata,
                                 raft_entry_t *ety, int) {
  assert(udata != nullptr && ety != nullptr);
  assert(!raft_entry_is_cfg_change(ety));

  auto *c = static_cast<AppContext *>(udata);
  assert(c->check_magic());

  c->server.raft_log.push_back(*ety);

  return 0;  // Ignored
}

// Raft callback for deleting the most recent entry from the log. This happens
// when an invalid leader finds a valid leader and has to delete superseded
// log entries.
static int __raft_logentry_pop(raft_server_t *, void *udata, raft_entry_t *,
                               int) {
  assert(udata != nullptr);
  auto *c = static_cast<AppContext *>(udata);
  assert(c->check_magic());

  raft_entry_t &entry = c->server.raft_log.back();
  if (entry.data.len == sizeof(size_t)) {
    // Handle counter pool buffers separately
    assert(entry.data.buf != nullptr);
    c->counter_buf_pool_free(entry.data.buf);
  } else {
    if (entry.data.buf != nullptr) free(entry.data.buf);
  }

  c->server.raft_log.pop_back();
  return 0;
}

// Raft callback for removing the first entry from the log. This is provided to
// support log compaction in the future.
static int __raft_logentry_poll(raft_server_t *, void *, raft_entry_t *, int) {
  printf("consensus: Ignoring __raft_logentry_poll callback.\n");
  return 0;
}

// Non-voting node now has enough logs to be able to vote. Append a finalization
// cfg log entry.
static int __raft_node_has_sufficient_logs(raft_server_t *, void *,
                                           raft_node_t *) {
  printf("consensus: Ignoring __raft_node_has_sufficient_logs callback.\n");
  return 0;
}

// Raft callback for displaying debugging information
void __raft_log(raft_server_t *, raft_node_t *, void *, const char *buf) {
  _unused(buf);

  if (kAppVerbose) {
    printf("raft: %s [%s].\n", buf, ERpc::get_formatted_time().c_str());
  }
}

void set_raft_callbacks(AppContext *c) {
  raft_cbs_t raft_funcs;
  raft_funcs.send_requestvote = __raft_send_requestvote;
  raft_funcs.send_appendentries = __raft_send_appendentries;
  raft_funcs.applylog = __raft_applylog;
  raft_funcs.persist_vote = __raft_persist_vote;
  raft_funcs.persist_term = __raft_persist_term;
  raft_funcs.log_offer = __raft_logentry_offer;
  raft_funcs.log_poll = __raft_logentry_poll;
  raft_funcs.log_pop = __raft_logentry_pop;
  raft_funcs.node_has_sufficient_logs = __raft_node_has_sufficient_logs;

  // Providing a non-null console log callback is expensive, even if we do
  // nothing in the callback.
  if (kAppEnableRaftConsoleLog) {
    raft_funcs.log = __raft_log;
  } else {
    raft_funcs.log = nullptr;
  }

  // Callback udata = context
  raft_set_callbacks(c->server.raft, &raft_funcs, static_cast<void *>(c));
}

#endif
