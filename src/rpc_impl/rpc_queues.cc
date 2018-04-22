#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_credit_stall_queue_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (SSlot *sslot : credit_stall_txq) {
    if (sslot->session->client_info.credits > 0) {
      client_kick_st(sslot);
    } else {
      credit_stall_txq[write_index++] = sslot;
    }
  }

  credit_stall_txq.resize(write_index);  // Number of sslots left = write_index
}

template <class TTr>
void Rpc<TTr>::process_wheel_st() {
  assert(in_dispatch());
  size_t cur_tsc = dpath_rdtsc();
  wheel->reap(cur_tsc);

  size_t num_ready = wheel->ready_queue.size();
  for (size_t i = 0; i < num_ready; i++) {
    wheel_ent_t &ent = wheel->ready_queue.front();
    assert(!ent.is_rfr);  // For now

    LOG_CC("eRPC Rpc %u: Req num %zu, pkt num %zu, actual TX %.3f us.\n",
           rpc_id, ent.sslot->cur_req_num, ent.pkt_num,
           to_usec(cur_tsc - creation_tsc, freq_ghz));

    enqueue_pkt_tx_burst_st(
        ent.sslot, ent.pkt_num, /* Packet index */
        &ent.sslot->client_info.tx_ts[ent.pkt_num % kSessionCredits]);

    wheel->ready_queue.pop();
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_request_st() {
  assert(in_dispatch());
  auto &queue = bg_queues.enqueue_request;
  size_t cmds_to_process = queue.size;  // We might re-add to the queue

  for (size_t i = 0; i < cmds_to_process; i++) {
    enq_req_args_t args = queue.unlocked_pop();
    enqueue_request(args.session_num, args.req_type, args.req_msgbuf,
                    args.resp_msgbuf, args.cont_func, args.tag, args.cont_etid);
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

FORCE_COMPILE_TRANSPORTS

}  // End erpc
