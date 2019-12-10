#include "common.h"
#include "nexus.h"
#include "rpc_types.h"
#include "session.h"
#include "util/mt_queue.h"

namespace erpc {

void Nexus::bg_thread_func(BgThreadCtx ctx) {
  ctx.tls_registry->init();  // Initialize thread-local variables

  // The BgWorkItem request list can be indexed using the background thread's
  // index in the Nexus, or its eRPC TID.
  assert(ctx.bg_thread_index == ctx.tls_registry->get_etid());
  ERPC_INFO("eRPC Nexus: Background thread %zu running. Tiny TID = %zu.\n",
            ctx.bg_thread_index, ctx.tls_registry->get_etid());

  while (*ctx.kill_switch == false) {
    if (ctx.bg_req_queue->size == 0) {
      // TODO: Put bg thread to sleep if it's idle for a long time
      continue;
    }

    while (ctx.bg_req_queue->size > 0) {
      BgWorkItem wi = ctx.bg_req_queue->unlocked_pop();

      if (wi.is_req()) {
        SSlot *s = wi.sslot;  // For requests, we have a valid sslot
        uint8_t req_type = s->server_info.req_type;
        const ReqFunc &req_func = ctx.req_func_arr->at(req_type);
        req_func.req_func(static_cast<ReqHandle *>(s), wi.context);
      } else {
        // For responses, we don't have a valid sslot
        wi.cont_func(wi.context, wi.tag);
      }
    }
  }

  ERPC_INFO("eRPC Nexus: Background thread %zu exiting.\n",
            ctx.bg_thread_index);
  return;
}

}  // namespace erpc
