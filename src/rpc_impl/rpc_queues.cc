#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_credit_stall_queue_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (SSlot *sslot : stallq) {
    if (sslot->session->client_info.credits > 0) {
      // sslots in stall queue have packets to send
      req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
    } else {
      stallq[write_index++] = sslot;
    }
  }

  stallq.resize(write_index);  // Number of sslots left = write_index
}

template <class TTr>
void Rpc<TTr>::process_wheel_st() {
  assert(in_dispatch());
  size_t cur_tsc = dpath_rdtsc();
  wheel->reap(cur_tsc);

  size_t num_ready = wheel->ready_queue.size();
  for (size_t i = 0; i < num_ready; i++) {
    // kick_req/rfr() cannot be used here. This packet has already used a credit
    // and gone through the wheel; the kick logic might repeat these.
    wheel_ent_t &ent = wheel->ready_queue.front();
    auto *sslot = reinterpret_cast<SSlot *>(ent.sslot);
    size_t pkt_num = ent.pkt_num;
    size_t crd_i = pkt_num % kSessionCredits;

    sslot->client_info.wheel_count--;
    sslot->client_info.wslot_idx[crd_i] = kWheelInvalidWslot;

    auto &ci = sslot->client_info;
    if (pkt_num < sslot->tx_msgbuf->num_pkts) {
      enqueue_pkt_tx_burst_st(sslot, pkt_num /* pkt_idx */, &ci.tx_ts[crd_i]);
    } else {
      MsgBuffer *resp_msgbuf = ci.resp_msgbuf;
      enqueue_rfr_st(sslot, resp_msgbuf->get_pkthdr_0());
    }

    LOG_CC(trace_file, "Rpc %u: lsn/req/pkt %u,%zu/%zu, reaped at %.3f us.\n",
           rpc_id, sslot->session->local_session_num, sslot->cur_req_num,
           pkt_num, to_usec(cur_tsc - creation_tsc, freq_ghz));

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
