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
  // index in the Nexus, or its ERpc TID.
  assert(ctx->bg_thread_index == ctx->tls_registry->get_etid());
  LOG_INFO("eRPC Nexus: Background thread %zu running. Tiny TID = %zu.\n",
           ctx->bg_thread_index, ctx->tls_registry->get_etid());

  while (*ctx->kill_switch == false) {
    if (ctx->bg_req_list.size == 0) {
      // Try again later
      usleep(1);
      continue;
    }

    ctx->bg_req_list.lock();
    assert(ctx->bg_req_list.size > 0);

    for (BgWorkItem wi : ctx->bg_req_list.list) {
      assert(wi.context != nullptr && wi.sslot != nullptr);

      // Sanity-check RX and TX MsgBuffers
      Rpc<TTr>::debug_check_bg_rx_msgbuf(wi.sslot, wi.wi_type);
      assert(wi.sslot->tx_msgbuf == nullptr);

      LOG_TRACE(
          "eRPC Background: Background thread %zu running %s for Rpc %u."
          "Request number = %zu.\n",
          ctx->bg_thread_index,
          wi.is_req() ? "request handler" : "continuation",
          wi.rpc->get_rpc_id(), wi.sslot->server_info.req_msgbuf.get_req_num());

      if (wi.is_req()) {
        uint8_t req_type = wi.sslot->server_info.req_msgbuf.get_req_type();
        const ReqFunc &req_func = ctx->req_func_arr->at(req_type);
        assert(req_func.is_registered());  // Checked during submit_bg

        req_func.req_func(static_cast<ReqHandle *>(wi.sslot), wi.context);
        wi.rpc->bury_req_msgbuf_server(wi.sslot);  // Bury dynamic req msgbuf
      } else {
        wi.sslot->client_info.cont_func(static_cast<RespHandle *>(wi.sslot),
                                        wi.context, wi.sslot->client_info.tag);

        // The continuation's release_response() will be handled by the
        // foreground thread later, so we can't check that it was called here.
      }
    }

    ctx->bg_req_list.locked_clear();
    ctx->bg_req_list.unlock();
  }

  LOG_INFO("eRPC Nexus: Background thread %zu exiting.\n",
           ctx->bg_thread_index);
  return;
}

}  // End ERpc
