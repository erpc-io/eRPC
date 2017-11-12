#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_request_st() {
  assert(in_creator());
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
  assert(in_creator());
  MtQueue<ReqHandle *> &queue = bg_queues.enqueue_response;

  while (queue.size > 0) {
    ReqHandle *req_handle = queue.unlocked_pop();
    enqueue_response(req_handle);
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_release_response_st() {
  assert(in_creator());
  MtQueue<RespHandle *> &queue = bg_queues.release_response;

  while (queue.size > 0) {
    RespHandle *resp_handle = queue.unlocked_pop();
    release_response(resp_handle);
  }
}

}  // End erpc
