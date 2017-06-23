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
    assert(msg_buffer != nullptr);
    assert(msg_buffer->buf != nullptr && msg_buffer->check_magic());

    assert(item.data_bytes <= kMaxDataPerPkt);
    assert(item.offset + item.data_bytes <= msg_buffer->data_size);

    if (item.data_bytes == 0) {
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
      // This is not the first packet, so we need 2 SGEs. The offset_to_pkt_num
      // function is expensive, but it's OK because we're dealing with a
      // multi-pkt message.
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

  // Handle failure. XXX: Don't exit.
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "ibv_post_send failed. ret = %d\n", ret);
    exit(-1);
  }

  send_wr[num_pkts - 1].next = &send_wr[num_pkts];  // Restore chain; safe
}

size_t IBTransport::rx_burst() {
  int new_comps = ibv_poll_cq(recv_cq, kPostlist, recv_wc);
  assert(new_comps >= 0);  // When can this fail?

  if (kDatapathChecks) {
    for (int i = 0; i < new_comps; i++) {
      if (unlikely(recv_wc[i].status != 0)) {
        fprintf(stderr, "Bad wc status %d\n", recv_wc[i].status);
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
  if (recvs_to_post < kRecvSlack) {
    return;
  }

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
