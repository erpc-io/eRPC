#ifndef BG_THREAD_H
#define BG_THREAD_H

#include <unistd.h>

#include "common.h"
#include "ops.h"
#include "session.h"
#include "util/mt_list.h"

namespace ERpc {

/// A work item submitted to a background thread.
/// Also acts as the background work completion.
class BgWorkItem {
 public:
  BgWorkItem(uint8_t app_tid, void *context, Session *session,
             Session::sslot_t *sslot)
      : app_tid(app_tid), context(context), session(session), sslot(sslot) {}

  const uint8_t app_tid;  ///< TID of the Rpc that submitted this request
  void *context;          ///< The context to use for request handler
  Session *session;
  Session::sslot_t *sslot;
};

/// Background thread context
class BgThreadCtx {
 public:
  /// A switch used by the Nexus to turn off background threads
  volatile bool *bg_kill_switch;

  /// A pointer to the Rpc's Ops. Unlike Rpc threads which create a copy of the
  /// Nexus's Ops, background threads have a pointer. This is because background
  /// threads are launched before any Ops are registered.
  std::array<Ops, kMaxReqTypes> *ops_arr;

  size_t bg_thread_id;             ///< ID of the background thread
  MtList<BgWorkItem> bg_req_list;  ///< Background thread request list

  /// Lists for submitting responses to Rpc threads
  MtList<BgWorkItem> *bg_resp_list_arr[kMaxAppTid + 1] = {nullptr};
};

/// The function executed by background RPC-processing threads
static void bg_thread_func(BgThreadCtx *bg_thread_ctx) {
  volatile bool *bg_kill_switch = bg_thread_ctx->bg_kill_switch;
  size_t bg_thread_id = bg_thread_ctx->bg_thread_id;

  while (*bg_kill_switch == false) {
    MtList<BgWorkItem> &req_list = bg_thread_ctx->bg_req_list;

    if (req_list.size == 0) {
      /* Try again later */
      usleep(1);
      continue;
    }

    req_list.lock();
    assert(req_list.size > 0);

    for (BgWorkItem bg_work_item : req_list.list) {
      uint8_t app_tid = bg_work_item.app_tid; /* Debug-only */
      void *context = bg_work_item.context;
      Session *session = bg_work_item.session; /* Debug-only */
      Session::sslot_t *sslot = bg_work_item.sslot;
      _unused(app_tid);
      _unused(session);

      dpath_dprintf(
          "eRPC Nexus: Background thread %zu running request "
          "handler for Rpc %u, session %u.\n",
          bg_thread_id, app_tid, session->local_session_num);

      /* Sanity-check rx_msgbuf. It must use dynamic memory allocation. */
      assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
      assert(sslot->rx_msgbuf.is_dynamic());
      assert(sslot->rx_msgbuf.is_req());

      uint8_t req_type = sslot->rx_msgbuf.get_req_type();
      const Ops &ops = bg_thread_ctx->ops_arr->at(req_type);
      assert(ops.is_valid()); /* Checked during submit_bg */

      ops.req_handler(&sslot->rx_msgbuf, &sslot->app_resp, context);

      /* Submit the response */
      MtList<BgWorkItem> *resp_list = bg_thread_ctx->bg_resp_list_arr[app_tid];
      assert(resp_list != nullptr);

      resp_list->unlocked_push_back(bg_work_item); /* Thread-safe */
    }

    req_list.locked_clear();
    req_list.unlock();
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu exiting.\n", bg_thread_id);
  return;
}

}  // End eRPC

#endif  // BG_THREAD_H
