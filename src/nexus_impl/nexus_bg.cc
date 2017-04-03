#include "nexus.h"
#include "common.h"
#include "ops.h"
#include "session.h"
#include "util/mt_list.h"

namespace ERpc {

template <class TTr>
void Nexus<TTr>::bg_thread_func(BgThreadCtx *bg_thread_ctx) {
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
      req_func.req_func((ReqHandle *)sslot, context);
    }

    req_list.locked_clear();
    req_list.unlock();
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu exiting.\n", bg_thread_id);
  return;
}

}  // End ERpc
