#include "nexus.h"
#include "common.h"
#include "ops.h"
#include "session.h"
#include "util/mt_queue.h"

namespace ERpc {

void Nexus::bg_thread_func(BgThreadCtx ctx) {
  ctx.tls_registry->init();  // Initialize thread-local variables

  // The BgWorkItem request list can be indexed using the background thread's
  // index in the Nexus, or its ERpc TID.
  assert(ctx.bg_thread_index == ctx.tls_registry->get_etid());
  LOG_INFO("eRPC Nexus: Background thread %zu running. Tiny TID = %zu.\n",
           ctx.bg_thread_index, ctx.tls_registry->get_etid());

  while (*ctx.kill_switch == false) {
    if (ctx.bg_req_queue->size == 0) {
      // Try again later
      usleep(1);
      continue;
    }

    MtQueue<BgWorkItem> *queue = ctx.bg_req_queue;
    size_t cmds_to_process = queue->size;

    for (size_t i = 0; i < cmds_to_process; i++) {
      BgWorkItem wi = queue->unlocked_pop();
      SSlot *s = wi.sslot;

      LOG_TRACE(
          "eRPC Background: Background thread %zu running %s for Rpc %u."
          "Request number = %zu.\n",
          ctx.bg_thread_index, wi.is_req() ? "request handler" : "continuation",
          wi.rpc_id, s->cur_req_num);

      if (wi.is_req()) {
        assert(!s->is_client && s->server_info.req_msgbuf.is_valid_dynamic());

        uint8_t req_type = s->server_info.req_msgbuf.get_req_type();
        const ReqFunc &req_func = ctx.req_func_arr->at(req_type);
        assert(req_func.is_registered());  // Checked during submit_bg

        req_func.req_func(static_cast<ReqHandle *>(s), wi.context);
      } else {
        assert(s->is_client && s->client_info.resp_msgbuf->is_valid_dynamic());

        wi.sslot->client_info.cont_func(static_cast<RespHandle *>(s),
                                        wi.context, s->client_info.tag);
      }
    }
  }

  LOG_INFO("eRPC Nexus: Background thread %zu exiting.\n", ctx.bg_thread_index);
  return;
}

}  // End ERpc
