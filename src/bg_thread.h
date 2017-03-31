#ifndef BG_THREAD_H
#define BG_THREAD_H

#include <unistd.h>

#include "common.h"
#include "ops.h"
#include "session.h"
#include "util/mt_list.h"

namespace ERpc {

/// A work item submitted to a background thread
class BgWorkItem {
 public:
  BgWorkItem(uint8_t app_tid, void *context, SSlot *sslot)
      : app_tid(app_tid), context(context), sslot(sslot) {}

  /// App TID of the Rpc that submitted this request. Debug-only.
  const uint8_t app_tid;
  void *context;  ///< The context to use for request handler
  SSlot *sslot;
};

/// Background thread context
class BgThreadCtx {
 public:
  /// A switch used by the Nexus to turn off background threads
  volatile bool *bg_kill_switch;

  /// A pointer to the Nexus's request functions. Unlike Rpc threads that create
  /// a copy of the Nexus's request functions, background threads have a
  /// pointer. This is because background threads are launched before any
  /// request functions are registered.
  std::array<ReqFunc, kMaxReqTypes> *req_func_arr;

  size_t bg_thread_id;             ///< ID of the background thread
  MtList<BgWorkItem> bg_req_list;  ///< Background thread request list
};

/// The function executed by background RPC-processing threads
static void bg_thread_func(BgThreadCtx *bg_thread_ctx) {
  volatile bool *bg_kill_switch = bg_thread_ctx->bg_kill_switch;
  size_t bg_thread_id = bg_thread_ctx->bg_thread_id;

  while (*bg_kill_switch == false) {
    MtList<BgWorkItem> &req_list = bg_thread_ctx->bg_req_list;

    if (req_list.size == 0) {
      // Try again later
      usleep(1);
      continue;
    }

    req_list.lock();
    assert(req_list.size > 0);

    for (BgWorkItem bg_work_item : req_list.list) {
      uint8_t app_tid = bg_work_item.app_tid;  // Debug-only
      void *context = bg_work_item.context;    // The app's context
      SSlot *sslot = bg_work_item.sslot;
      Session *session = sslot->session;  // Debug-only
      _unused(app_tid);
      _unused(session);

      // Sanity-check rx_msgbuf. It must use dynamic memory allocation.
      assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
      assert(sslot->rx_msgbuf.is_dynamic());
      assert(sslot->rx_msgbuf.is_req());

      assert(sslot->tx_msgbuf == nullptr);  // Sanity-check tx_msgbuf

      dpath_dprintf(
          "eRPC Background: Background thread %zu running request "
          "handler for Rpc %u, session %u. Request number = %zu.\n",
          bg_thread_id, app_tid, session->local_session_num,
          sslot->rx_msgbuf.get_req_num());

      uint8_t req_type = sslot->rx_msgbuf.get_req_type();
      const ReqFunc &req_func = bg_thread_ctx->req_func_arr->at(req_type);
      assert(req_func.is_registered());  // Checked during submit_bg

      // We don't have access to the Rpc object: it's templated and we don't
      // want a templated Nexus. So, rx_msgbuf will be buried when the user
      // calls enqueue_response().
      req_func.req_func((ReqHandle *)sslot, &sslot->rx_msgbuf, context);
    }

    req_list.locked_clear();
    req_list.unlock();
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu exiting.\n", bg_thread_id);
  return;
}

}  // End eRPC

#endif  // BG_THREAD_H
