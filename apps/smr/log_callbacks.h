/**
 * @file log_callbacks.h
 * SMR log record callbacks
 */

#pragma once
#include "smr.h"

static int __raft_send_snapshot(raft_server_t *, void *, raft_node_t *) {
  erpc::rt_assert(false, "Snapshots not supported");
  return -1;
}

// Raft callback for applying an entry to the log at position \p entry_idx
static int __raft_log_offer(raft_server_t *, void *udata, raft_entry_t *ety,
                            raft_index_t entry_idx) {
  assert(!raft_entry_is_cfg_change(ety));
  auto *c = static_cast<AppContext *>(udata);

  if (kUsePmem) {
    erpc::rt_assert(static_cast<size_t>(entry_idx) ==
                    c->server.pmem.v.num_entries);

    // Make a volatile copy
    pmem_ser_logentry_t v_log_entry;
    v_log_entry.raft_entry = *ety;
    v_log_entry.client_req = *reinterpret_cast<client_req_t *>(ety->data.buf);
    erpc::rt_assert(ety->data.len == sizeof(client_req_t));

    pmem_ser_logentry_t *p_log_entry_ptr =
        &c->server.pmem.v.log_entries_base[c->server.pmem.v.num_entries];
    c->server.pmem.v.num_entries++;

    size_t cycles_start = 0;
    if (kAppMeasurePmemLatency) cycles_start = erpc::rdtsc();

    // First update log data, then log tail
    pmem_memcpy_persist(p_log_entry_ptr, &v_log_entry, sizeof(v_log_entry));
    pmem_memcpy_persist(c->server.pmem.p.num_entries,
                        &c->server.pmem.v.num_entries,
                        sizeof(c->server.pmem.v.num_entries));

    if (kAppMeasurePmemLatency) {
      size_t ns =
          erpc::to_nsec(erpc::rdtsc() - cycles_start, c->rpc->get_freq_ghz());
      if (unlikely(ns > kAppPmemNsecMax)) {
        fprintf(stderr, "%zu ns is larger than expected pmem latency.\n", ns);
      }
      hdr_record_value(c->server.pmem_nsec_hdr, static_cast<int64_t>(ns));
    }
  } else {
    erpc::rt_assert(static_cast<size_t>(entry_idx) ==
                    c->server.dram_raft_log.size());
    c->server.dram_raft_log.push_back(*ety);
  }
  return 0;
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
static int __raft_persist_vote(raft_server_t *, void *udata,
                               raft_node_id_t voted_for) {
  if (kUsePmem) {
    auto *c = static_cast<AppContext *>(udata);
    pmem_memcpy_persist(c->server.pmem.p.voted_for, &voted_for,
                        sizeof(voted_for));
  }

  return 0;  // Ignored for DRAM mode
}

// Raft callback for saving term and voted_for field to persistent storage
static int __raft_persist_term(raft_server_t *, void *udata, raft_term_t term,
                               raft_node_id_t voted_for) {
  erpc::rt_assert(term < UINT32_MAX, "Term too large");
  if (kUsePmem) {
    auto *c = static_cast<AppContext *>(udata);
    erpc::rt_assert(reinterpret_cast<uint8_t *>(c->server.pmem.p.voted_for) ==
                    reinterpret_cast<uint8_t *>(c->server.pmem.p.term) + 4);

    // 8-byte atomic commit
    uint32_t to_persist[2];
    to_persist[0] = term;
    to_persist[1] = static_cast<uint32_t>(voted_for);
    pmem_memcpy_persist(c->server.pmem.p.term, &to_persist, sizeof(size_t));
  }
  return 0;  // Ignored for DRAM mode
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

  if (kUsePmem) {
  } else {
    raft_entry_t &entry = c->server.dram_raft_log.back();
    if (likely(entry.data.len == sizeof(client_req_t))) {
      // Handle pool-allocated buffers separately
      assert(entry.data.buf != nullptr);
      c->server.log_entry_appdata_pool.free(
          static_cast<client_req_t *>(entry.data.buf));
    } else {
      if (entry.data.buf != nullptr) free(entry.data.buf);
    }
    c->server.dram_raft_log.pop_back();
  }

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
