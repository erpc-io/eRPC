#include "nexus.h"
#include "common.h"
#include "ops.h"
#include "rpc.h"
#include "session.h"
#include "util/mt_list.h"

namespace ERpc {

template <class TTr>
void Nexus<TTr>::bg_thread_func(BgThreadCtx *ctx) {
  ctx->tls_registry->init();  // Initialize thread-local variables

  // The BgWorkItem request list can be indexed using the background thread's
  // index in the Nexus, or its tiny TID.
  assert(ctx->bg_thread_index == ctx->tls_registry->get_tiny_tid());
  erpc_dprintf("eRPC Nexus: Background thread %zu running. Tiny TID = %zu.\n",
               ctx->bg_thread_index, ctx->tls_registry->get_tiny_tid());

  while (*ctx->kill_switch == false) {
    if (ctx->bg_req_list.size == 0) {
      // Try again later
      usleep(1);
      continue;
    }

    ctx->bg_req_list.lock();
    assert(ctx->bg_req_list.size > 0);

    for (BgWorkItem wi : ctx->bg_req_list.list) {
      assert(wi.context != nullptr && wi.sslot != nullptr &&
             wi.sslot->session != nullptr);

      // Sanity-check RX and TX MsgBuffers
      Rpc<TTr>::debug_check_bg_rx_msgbuf(wi.sslot, wi.wi_type);
      assert(wi.sslot->tx_msgbuf == nullptr);

      dpath_dprintf(
          "eRPC Background: Background thread %zu running %s for Rpc %u, "
          "session %u. Request number = %zu.\n",
          ctx->bg_thread_index,
          wi.is_req() ? "request handler" : "continuation",
          wi.rpc->get_rpc_id(), wi.sslot->session->local_session_num,
          wi.sslot->rx_msgbuf.get_req_num());

      if (wi.is_req()) {
        uint8_t req_type = wi.sslot->rx_msgbuf.get_req_type();
        const ReqFunc &req_func = ctx->req_func_arr->at(req_type);
        assert(req_func.is_registered());  // Checked during submit_bg

        req_func.req_func(static_cast<ReqHandle *>(wi.sslot), wi.context);
        wi.rpc->bury_sslot_rx_msgbuf(wi.sslot);
      } else {
        wi.sslot->clt_save_info.cont_func(static_cast<RespHandle *>(wi.sslot),
                                          wi.context,
                                          wi.sslot->clt_save_info.tag);

        // The continuation must release the response (rx_msgbuf), but the
        // event loop thread may re-use it. So it may not be null.
      }
    }

    ctx->bg_req_list.locked_clear();
    ctx->bg_req_list.unlock();
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu exiting.\n",
               ctx->bg_thread_index);
  return;
}

}  // End ERpc
