#include "ib_transport.h"

namespace ERpc {

// Packets that are the first packet in their MsgBuffer use one DMA, and may
// be inlined. Packets that are not the first packet use two DMAs, and are never
// inlined for simplicity.
void IBTransport::tx_burst(const tx_burst_item_t* tx_burst_arr,
                           size_t num_pkts) {
  assert(tx_burst_arr != nullptr);

  for (size_t i = 0; i < num_pkts; i++) {
    const tx_burst_item_t& item = tx_burst_arr[i];
    assert(item.routing_info != nullptr);
    assert(item.msg_buffer != nullptr);

    const MsgBuffer* msg_buffer = item.msg_buffer;
    assert(msg_buffer->is_valid());  // Can be fake for control packets

    assert(item.data_bytes <= kMaxDataPerPkt);
    assert(item.offset + item.data_bytes <= msg_buffer->data_size);

    if (small_rpc_unlikely(item.data_bytes == 0)) {
      assert(msg_buffer->is_expl_cr() || msg_buffer->is_req_for_resp());
    }

    // Verify constant fields of work request
    struct ibv_send_wr& wr = send_wr[i];
    struct ibv_sge* sgl = send_sgl[i];

    assert(wr.next == &send_wr[i + 1]);  // +1 is valid
    assert(wr.wr.ud.remote_qkey == kQKey);
    assert(wr.opcode == IBV_WR_SEND_WITH_IMM);
    assert(wr.sg_list == sgl);

    // Set signaling flag. The work request is non-inline by default.
    wr.send_flags = get_signaled_flag();

    if (small_rpc_likely(item.offset == 0)) {
      // This is the first packet, so we need only 1 SGE. This can be a credit
      // return packet.
      pkthdr_t* pkthdr = msg_buffer->get_pkthdr_0();
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t) + item.data_bytes);
      sgl[0].lkey = msg_buffer->buffer.lkey;

      // Only single-SGE work requests are inlined
      wr.send_flags |= (sgl[0].length <= kMaxInline) ? IBV_SEND_INLINE : 0;
      wr.num_sge = 1;
    } else {
      // This is not the first packet, so we need 2 SGEs. The offset-to-pkt_num
      // computation involes a division, but it's OK because it's a large msg.
      size_t pkt_num = (item.offset + kMaxDataPerPkt - 1) / kMaxDataPerPkt;
      const pkthdr_t* pkthdr = msg_buffer->get_pkthdr_n(pkt_num);
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t));
      sgl[0].lkey = msg_buffer->buffer.lkey;

      send_sgl[i][1].addr =
          reinterpret_cast<uint64_t>(&msg_buffer->buf[item.offset]);
      send_sgl[i][1].length = static_cast<uint32_t>(item.data_bytes);
      send_sgl[i][1].lkey = msg_buffer->buffer.lkey;

      wr.num_sge = 2;
    }

    const ib_routing_info_t* ib_routing_info =
        reinterpret_cast<ib_routing_info_t*>(item.routing_info);

    if (kFaultInjection && item.drop) {
      // Remote QPN = 0 is reserved by IB, so the packet will be dropped
      wr.wr.ud.remote_qpn = 0;
    } else {
      wr.wr.ud.remote_qpn = ib_routing_info->qpn;
    }
    wr.wr.ud.ah = ib_routing_info->ah;
  }

  send_wr[num_pkts - 1].next = nullptr;  // Breaker of chains

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);

  assert(ret == 0);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "eRPC: Fatal error. ibv_post_send failed. ret = %d\n", ret);
    exit(-1);
  }

  send_wr[num_pkts - 1].next = &send_wr[num_pkts];  // Restore chain; safe
}

// This is a slower polling function than the one used in tx_burst: it prints
// a warning message when the number of polling attempts gets too high. This
// overhead is fine because the send queue is flushed only on packet loss or
// reordering.
void IBTransport::poll_send_cq_for_flush(bool first) {
  struct ibv_wc wc;
  size_t num_tries = 0;
  while (ibv_poll_cq(send_cq, 1, &wc) == 0) {
    num_tries++;
    if (num_tries == 1000000000) {
      fprintf(stderr,
              "eRPC: Warning. tx_flush stuck polling for %s signaled wr.\n",
              first ? "first" : "second");
      num_tries = 0;
    }
  }

  if (unlikely(wc.status != 0)) {
    fprintf(stderr, "eRPC: Fatal error. Bad SEND wc status %d\n", wc.status);
    exit(-1);
  }
}

void IBTransport::tx_flush() {
  if (unlikely(nb_tx == 0)) {
    fprintf(stderr,
            "eRPC: Warning. tx_flush called, but no SEND request in queue.\n");
    return;
  }

  // If we are here, we have posted a SEND work request. The selective signaling
  // logic guarantees that there is *exactly one* *signaled* SEND work request.
  poll_send_cq_for_flush(true);  // Poll the one existing signaled WQE

  // Use send_wr[0] to post the second signaled flush WQE
  struct ibv_send_wr& wr = send_wr[0];
  struct ibv_sge* sgl = send_sgl[0];

  assert(wr.next == &send_wr[1]);  // +1 is valid
  assert(wr.wr.ud.remote_qkey == kQKey);
  assert(wr.opcode == IBV_WR_SEND_WITH_IMM);
  assert(wr.sg_list == send_sgl[0]);

  // We could use a header-only SEND, but the optimized inline-copy function in
  // the modded driver expects WQEs with exactly one SGE.
  char flush_inline_buf[1];
  sgl[0].addr = reinterpret_cast<uint64_t>(flush_inline_buf);
  sgl[0].length = 1;

  wr.next = nullptr;  // Break the chain
  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
  wr.num_sge = 1;
  wr.wr.ud.remote_qpn = 0;  // Invalid QPN, which will cause the drop
  wr.wr.ud.ah = self_ah;    // Send to self

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);

  assert(ret == 0);
  if (unlikely(ret != 0)) {
    fprintf(stderr,
            "eRPC: Fatal error. ibv_post_send failed for flush WQE. ret = %d\n",
            ret);
    exit(-1);
  }

  wr.next = &send_wr[1];  // Restore the chain

  poll_send_cq_for_flush(false);  // Poll the signaled WQE posted above
  nb_tx = 0;                      // Reset signaling logic
}

size_t IBTransport::rx_burst() {
  int new_comps = ibv_poll_cq(recv_cq, kPostlist, recv_wc);
  assert(new_comps >= 0);  // When can this fail?

  if (kDatapathChecks) {
    for (int i = 0; i < new_comps; i++) {
      if (unlikely(recv_wc[i].status != 0)) {
        fprintf(stderr, "eRPC: Fatal error. Bad wc status %d\n",
                recv_wc[i].status);
        exit(-1);
      }
    }
  }

  return static_cast<size_t>(new_comps);
}

void IBTransport::post_recvs(size_t num_recvs) {
  assert(!fast_recv_used);               // Not supported yet
  assert(num_recvs <= kRecvQueueDepth);  // num_recvs can be 0
  assert(recvs_to_post < kRecvSlack);

  recvs_to_post += num_recvs;
  if (recvs_to_post < kRecvSlack) return;

  if (use_fast_recv) {
    // Construct a special RECV wr that the modded driver understands. Encode
    // the number of required RECVs in its num_sge field.
    struct ibv_recv_wr special_wr;
    special_wr.wr_id = kMagicWrIDForFastRecv;
    special_wr.num_sge = recvs_to_post;

    struct ibv_recv_wr* bad_wr = &special_wr;
    int ret = ibv_post_recv(qp, nullptr, &bad_wr);
    if (unlikely(ret != 0)) {
      fprintf(stderr, "eRPC IBTransport: Post RECV (fast) error %d\n", ret);
      exit(-1);
    }

    // Reset slack counter
    recvs_to_post = 0;
    return;
  }

  // The recvs posted are @first_wr through @last_wr, inclusive
  struct ibv_recv_wr *first_wr, *last_wr, *temp_wr, *bad_wr;

  int ret;
  size_t first_wr_i = recv_head;
  size_t last_wr_i = first_wr_i + (recvs_to_post - 1);
  if (last_wr_i >= kRecvQueueDepth) {
    last_wr_i -= kRecvQueueDepth;
  }

  first_wr = &recv_wr[first_wr_i];
  last_wr = &recv_wr[last_wr_i];
  temp_wr = last_wr->next;

  last_wr->next = nullptr;  // Breaker of chains

  ret = ibv_post_recv(qp, first_wr, &bad_wr);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "eRPC IBTransport: Post RECV (normal) error %d\n", ret);
    exit(-1);
  }

  last_wr->next = temp_wr;  // Restore circularity

  // Update RECV head: go to the last wr posted and take 1 more step
  recv_head = last_wr_i;
  recv_head = mod_add_one<kRecvQueueDepth>(recv_head);

  // Reset slack counter
  recvs_to_post = 0;
}

}  // End ERpc
