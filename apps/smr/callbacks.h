/**
 * @file callbacks.h
 * Callbacks for Raft
 */

#pragma once

#include "appendentries.h"
#include "requestvote.h"
#include "smr.h"

static int __raft_send_snapshot(raft_server_t *, void *, raft_node_t *) {
  erpc::rt_assert(false, "Snapshots not supported");
  return -1;
}

// Raft callback for applying an entry to the FSM
static int __raft_applylog(raft_server_t *, void *udata, raft_entry_t *ety,
                           raft_index_t) {
  assert(!raft_entry_is_cfg_change(ety));

  // We're applying an entry to the application's state machine, so we're sure
  // about its length. Other log callbacks can be invoked for non-application
  // log entries.
  assert(ety->data.len == sizeof(client_req_t));
  auto *client_req = reinterpret_cast<client_req_t *>(ety->data.buf);
  assert(client_req->key[0] == client_req->value[0]);

  auto *c = static_cast<AppContext *>(udata);

  if (kAppVerbose) {
    printf("smr: Applying log entry %s received at Raft server %u [%s].\n",
           client_req->to_string().c_str(), ety->id,
           erpc::get_formatted_time().c_str());
  }

  size_t key_hash = mica::util::hash(&client_req->key, kAppKeySize);
  FixedTable *table = c->server.table;
  FixedTable::ft_key_t *ft_key =
      reinterpret_cast<FixedTable::ft_key_t *>(client_req->key);

  auto result = table->set(key_hash, *ft_key,
                           reinterpret_cast<char *>(&client_req->value));
  erpc::rt_assert(result == mica::table::Result::kSuccess);
  return 0;
}

// Raft callback for saving voted_for field to persistent storage.
static int __raft_persist_vote(raft_server_t *, void *, const int) {
  return 0;  // Ignored
}

// Raft callback for saving term field to persistent storage
static int __raft_persist_term(raft_server_t *, void *, raft_term_t,
                               raft_node_id_t) {
  return 0;  // Ignored
}

// Raft callback for appending an item to the log
static int __raft_log_offer(raft_server_t *, void *udata, raft_entry_t *ety,
                            raft_index_t) {
  assert(!raft_entry_is_cfg_change(ety));

  auto *c = static_cast<AppContext *>(udata);
  c->server.raft_log.push_back(*ety);
  return 0;
}

// Raft callback for removing the first entry from the log. This is provided to
// support log compaction in the future.
static int __raft_log_poll(raft_server_t *, void *, raft_entry_t *,
                           raft_index_t) {
  erpc::rt_assert(false, "Log compaction not supported");
  return -1;
}

// Raft callback for deleting the most recent entry from the log. This happens
// when an invalid leader finds a valid leader and has to delete superseded
// log entries.
static int __raft_log_pop(raft_server_t *, void *udata, raft_entry_t *,
                          raft_index_t) {
  auto *c = static_cast<AppContext *>(udata);

  raft_entry_t &entry = c->server.raft_log.back();
  if (likely(entry.data.len == sizeof(client_req_t))) {
    // Handle RSM command pool buffers separately
    assert(entry.data.buf != nullptr);
    c->server.rsm_cmd_buf_pool.free(
        static_cast<client_req_t *>(entry.data.buf));
  } else {
    if (entry.data.buf != nullptr) free(entry.data.buf);
  }

  c->server.raft_log.pop_back();
  return 0;
}

// Raft callback for determining which node this configuration log entry affects
static int __raft_log_get_node_id(raft_server_t *, void *, raft_entry_t *,
                                  raft_index_t) {
  erpc::rt_assert(false, "Configuration change not supported");
  return -1;
}

// Non-voting node now has enough logs to be able to vote. Append a finalization
// cfg log entry.
static int __raft_node_has_sufficient_logs(raft_server_t *, void *,
                                           raft_node_t *) {
  printf("smr: Ignoring __raft_node_has_sufficient_logs callback.\n");
  return 0;
}

// Callback for being notified of membership changes. Implementing this callback
// is optional.
static void __raft_notify_membership_event(raft_server_t *, void *,
                                           raft_node_t *, raft_membership_e) {
  printf("smr: Ignoring __raft_notify_membership_event callback .\n");
}

// Raft callback for displaying debugging information
void __raft_log(raft_server_t *, raft_node_t *, void *, const char *buf) {
  _unused(buf);

  if (kAppVerbose) {
    printf("raft: %s [%s].\n", buf, erpc::get_formatted_time().c_str());
  }
}

void set_raft_callbacks(AppContext *c) {
  raft_cbs_t raft_funcs;
  raft_funcs.send_requestvote = __raft_send_requestvote;
  raft_funcs.send_appendentries = __raft_send_appendentries;
  raft_funcs.send_snapshot = __raft_send_snapshot;
  raft_funcs.applylog = __raft_applylog;
  raft_funcs.persist_vote = __raft_persist_vote;
  raft_funcs.persist_term = __raft_persist_term;
  raft_funcs.log_offer = __raft_log_offer;
  raft_funcs.log_poll = __raft_log_poll;
  raft_funcs.log_pop = __raft_log_pop;
  raft_funcs.log_get_node_id = __raft_log_get_node_id;
  raft_funcs.node_has_sufficient_logs = __raft_node_has_sufficient_logs;
  raft_funcs.notify_membership_event = __raft_notify_membership_event;

  // Providing a non-null console log callback is expensive, even if we do
  // nothing in the callback.
  raft_funcs.log = kAppEnableRaftConsoleLog ? __raft_log : nullptr;

  // Callback udata = context
  raft_set_callbacks(c->server.raft, &raft_funcs, static_cast<void *>(c));
}
