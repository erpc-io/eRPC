#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_request_st() {
  assert(in_creator());
  auto &queue = bg_queues.enqueue_request;

  queue.lock(); // Prevent background threads from enqueueing requests
  size_t write_index = 0;  // Requests that we can't enqueue are re-added here

  for (size_t i = 0; i < queue.size; i++) {
    enqueue_request_args_t &req_args = queue.list[i];
    int ret = enqueue_request(req_args.session_num, req_args.req_type,
                              req_args.req_msgbuf, req_args.cont_func,
                              req_args.tag);

    assert(ret == 0 || ret == -ENOMEM);  // XXX: Handle other failures

    if (ret == -ENOMEM) {
      // This means that the session is out of sslots
      queue.list[write_index++] = req_args;
    }
  }

  queue.list.resize(write_index);  // There are write_index elements left
  queue.unlock();
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_response_st() {
  assert(in_creator());
  MtList<ReqHandle *> &queue = bg_queues.enqueue_response;

  queue.lock(); // Prevent background threads from enqueueing responses

  for (ReqHandle *req_handle : queue.list) {
    enqueue_response(req_handle);
  }

  queue.locked_clear();
  queue.unlock();
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_release_response_st() {
  assert(in_creator());
  MtList<RespHandle *> &queue = bg_queues.release_response;

  queue.lock(); // Prevent background threads from enqueueing responses

  for (RespHandle *resp_handle : queue.list) {
    release_response(resp_handle);
  }

  queue.locked_clear();
  queue.unlock();
}


}  // End ERpc
