#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_credit_stall_queue_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (SSlot *sslot : credit_stall_txq) {
    // Try early exit
    if (sslot->session->client_info.credits == 0) {
      credit_stall_txq[write_index++] = sslot;
      continue;
    }

    bool all_pkts_tx = try_req_sslot_tx_st(sslot);
    if (!all_pkts_tx) credit_stall_txq[write_index++] = sslot;
  }

  credit_stall_txq.resize(write_index);  // Number of sslots left = write_index
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_request_st() {
  assert(in_dispatch());
  auto &queue = bg_queues.enqueue_request;
  size_t cmds_to_process = queue.size;  // We might re-add to the queue

  for (size_t i = 0; i < cmds_to_process; i++) {
    enqueue_request_args_t req_args = queue.unlocked_pop();
    int ret =
        enqueue_request(req_args.session_num, req_args.req_type,
                        req_args.req_msgbuf, req_args.resp_msgbuf,
                        req_args.cont_func, req_args.tag, req_args.cont_etid);

    assert(ret == 0 || ret == -ENOMEM);  // XXX: Handle other failures
    if (ret == -ENOMEM) queue.unlocked_push(req_args);  // Session out of sslots
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_response_st() {
  assert(in_dispatch());
  MtQueue<ReqHandle *> &queue = bg_queues.enqueue_response;

  while (queue.size > 0) {
    ReqHandle *req_handle = queue.unlocked_pop();
    enqueue_response(req_handle);
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_release_response_st() {
  assert(in_dispatch());
  MtQueue<RespHandle *> &queue = bg_queues.release_response;

  while (queue.size > 0) {
    RespHandle *resp_handle = queue.unlocked_pop();
    release_response(resp_handle);
  }
}

}  // End erpc
