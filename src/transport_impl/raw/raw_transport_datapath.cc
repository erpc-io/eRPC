#ifdef ERPC_RAW

#include "raw_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

void RawTransport::tx_burst(const tx_burst_item_t* tx_burst_arr,
                            size_t num_pkts) {
  for (size_t i = 0; i < num_pkts; i++) {
    const tx_burst_item_t& item = tx_burst_arr[i];
    const MsgBuffer* msg_buffer = item.msg_buffer;

    // Verify constant fields of work request
    struct ibv_send_wr& wr = send_wr[i];
    struct ibv_sge* sgl = send_sgl[i];

    assert(wr.next == &send_wr[i + 1]);  // +1 is valid
    assert(wr.opcode == IBV_WR_SEND);
    assert(wr.sg_list == sgl);

    // Set signaling + poll SEND CQ if needed. The wr is non-inline by default.
    wr.send_flags = get_signaled_flag() ? IBV_SEND_SIGNALED : 0;

    size_t pkt_size;
    pkthdr_t* pkthdr;
    if (item.pkt_idx == 0) {
      // This is the first packet, so we need only 1 SGE. This can be CR/RFR.
      pkthdr = msg_buffer->get_pkthdr_0();
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = msg_buffer->get_pkt_size<kMaxDataPerPkt>(0);
      sgl[0].lkey = msg_buffer->buffer.lkey;

      if (kMaxInline > 0 &&
          sgl[0].length <= kMaxInline + MLX5_ETH_INLINE_HEADER_SIZE) {
        wr.send_flags |= IBV_SEND_INLINE;
      }

      pkt_size = sgl[0].length;
      wr.num_sge = 1;
    } else {
      // This is not the first packet, so we need 2 SGEs
      pkthdr = msg_buffer->get_pkthdr_n(item.pkt_idx);
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t));
      sgl[0].lkey = msg_buffer->buffer.lkey;

      size_t offset = item.pkt_idx * kMaxDataPerPkt;
      sgl[1].addr = reinterpret_cast<uint64_t>(&msg_buffer->buf[offset]);
      sgl[1].length = std::min(kMaxDataPerPkt, msg_buffer->data_size - offset);
      sgl[1].lkey = msg_buffer->buffer.lkey;

      pkt_size = sgl[0].length + sgl[1].length;
      wr.num_sge = 2;
    }

    // We can do an 8-byte aligned memcpy as the 2-byte UDP csum is already 0
    static constexpr size_t hdr_copy_sz = kInetHdrsTotSize - 2;
    static_assert(hdr_copy_sz == 40, "");
    memcpy(&pkthdr->headroom[0], item.routing_info, hdr_copy_sz);

    if (kTesting && item.drop) {
      // XXX: Can this cause performance problems?
      auto* eth_hdr = reinterpret_cast<eth_hdr_t*>(pkthdr->headroom);
      memset(&eth_hdr->dst_mac, 0, sizeof(eth_hdr->dst_mac));
    }

    auto* ipv4_hdr =
        reinterpret_cast<ipv4_hdr_t*>(&pkthdr->headroom[sizeof(eth_hdr_t)]);
    assert(ipv4_hdr->check == 0);
    ipv4_hdr->tot_len = htons(pkt_size - sizeof(eth_hdr_t));

    auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);
    assert(udp_hdr->check == 0);
    udp_hdr->len = htons(pkt_size - sizeof(eth_hdr_t) - sizeof(ipv4_hdr_t));

    ERPC_TRACE(
        "eRPC RawTransport: Sending packet (idx = %zu, drop = %u). SGE #1 %uB, "
        " SGE #2 = %uB. pkthdr = %s. Frame header = %s.\n",
        i, item.drop, sgl[0].length, (wr.num_sge == 2 ? sgl[1].length : 0),
        pkthdr->to_string().c_str(),
        frame_header_to_string(&pkthdr->headroom[0]).c_str());
  }

  send_wr[num_pkts - 1].next = nullptr;  // Breaker of chains, Khaleesi of grass

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "eRPC: Fatal error. ibv_post_send failed. ret = %d\n", ret);
    assert(ret == 0);
    exit(-1);
  }

  send_wr[num_pkts - 1].next = &send_wr[num_pkts];  // Restore chain; safe
}

void RawTransport::tx_flush() {
  if (unlikely(nb_tx == 0)) return;  // There's nothing in the DMA queue

  // If we are here, we have sent a packet. The selective signaling logic
  // guarantees that there is *exactly one* *signaled* SEND work request.
  poll_cq_one_helper(send_cq);  // Poll the one existing signaled WQE

  size_t pkt_size = kInetHdrsTotSize + 8;
  Buffer buffer = huge_alloc->alloc(pkt_size);  // Get a registered buffer
  assert(buffer.buf != nullptr);

  memset(buffer.buf, 0, pkt_size);
  auto* pkthdr = reinterpret_cast<pkthdr_t*>(buffer.buf);

  // Create a valid packet to self, but later we'll garble the destination IP
  RoutingInfo self_ri;
  fill_local_routing_info(&self_ri);
  resolve_remote_routing_info(&self_ri);

  memcpy(&pkthdr->headroom[0], &self_ri, sizeof(RoutingInfo));

  auto* ipv4_hdr =
      reinterpret_cast<ipv4_hdr_t*>(&pkthdr->headroom[sizeof(eth_hdr_t)]);
  assert(ipv4_hdr->check == 0);
  ipv4_hdr->tot_len = htons(pkt_size - sizeof(eth_hdr_t));
  ipv4_hdr->dst_ip = 0;  // Dropped by switch, fast

  auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);
  assert(udp_hdr->check == 0);
  udp_hdr->len = htons(pkt_size - sizeof(eth_hdr_t) - sizeof(ipv4_hdr_t));

  // Use send_wr[0] to post the second signaled flush WQE
  struct ibv_send_wr& wr = send_wr[0];
  struct ibv_sge* sgl = send_sgl[0];

  assert(wr.next == &send_wr[1]);  // +1 is valid
  assert(wr.opcode == IBV_WR_SEND);
  assert(wr.sg_list == send_sgl[0]);

  sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
  sgl[0].length = pkt_size;
  sgl[0].lkey = buffer.lkey;

  wr.next = nullptr;                  // Break the chain
  wr.send_flags = IBV_SEND_SIGNALED;  // Not inlined!
  wr.num_sge = 1;

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "tx_flush post_send() failed. ret = %d\n", ret);
    assert(ret == 0);
    exit(-1);
  }

  wr.next = &send_wr[1];  // Restore the chain

  poll_cq_one_helper(send_cq);  // Poll the signaled WQE posted above
  nb_tx = 0;                    // Reset signaling logic

  huge_alloc->free_buf(buffer);
  testing.tx_flush_count++;
}

size_t RawTransport::rx_burst() {
  if (kDumb) {
    cqe_snapshot_t cur_snapshot;
    snapshot_cqe(&recv_cqe_arr[cqe_idx], cur_snapshot);
    const size_t delta = get_cqe_cycle_delta(prev_snapshot, cur_snapshot);
    if (delta == 0 || delta >= kNumRxRingEntries) return 0;

    for (size_t i = 0; i < delta; i++) {
      auto* pkthdr =
          reinterpret_cast<pkthdr_t*>(&ring_extent.buf[recv_head * kRecvSize]);
      __builtin_prefetch(pkthdr, 0, 3);

      ERPC_TRACE(
          "eRPC RawTransport: Received pkt. pkthdr = %s. Frame header = %s.\n",
          pkthdr->to_string().c_str(),
          frame_header_to_string(&pkthdr->headroom[0]).c_str());

      recv_head = (recv_head + 1) % kNumRxRingEntries;
    }

    // We can move to next CQE only after the current is used (i.e., delta > 0)
    cqe_idx = (cqe_idx + 1) % kRecvCQDepth;
    prev_snapshot = cur_snapshot;
    return delta;
  } else {
    int ret = ibv_poll_cq(recv_cq, kPostlist, recv_wc);
    assert(ret >= 0);
    return static_cast<size_t>(ret);
  }
}

void RawTransport::post_recvs(size_t num_recvs) {
  assert(num_recvs <= kNumRxRingEntries);  // num_recvs can be 0
  recvs_to_post += num_recvs;

  if (kDumb) {
    if (recvs_to_post < kStridesPerWQE) return;

    int ret = wq_family->recv_burst(wq, &mp_recv_sge[mp_sge_idx], 1);
    _unused(ret);
    assert(ret == 0);
    mp_sge_idx = (mp_sge_idx + 1) % kRQDepth;
    recvs_to_post -= kStridesPerWQE;  // Reset slack counter
  } else {
    if (recvs_to_post < kRecvSlack) return;

    if (kFastRecv) {
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

    size_t first_wr_i = recv_head;
    size_t last_wr_i = first_wr_i + (recvs_to_post - 1);
    if (last_wr_i >= kRQDepth) last_wr_i -= kRQDepth;

    first_wr = &recv_wr[first_wr_i];
    last_wr = &recv_wr[last_wr_i];
    temp_wr = last_wr->next;

    last_wr->next = nullptr;  // Breaker of chains, the Unburnt

    int ret = ibv_post_recv(qp, first_wr, &bad_wr);
    if (unlikely(ret != 0)) {
      fprintf(stderr, "eRPC IBTransport: Post RECV (normal) error %d\n", ret);
      exit(-1);
    }

    last_wr->next = temp_wr;  // Restore circularity

    // Update RECV head: go to the last wr posted and take 1 more step
    recv_head = last_wr_i;
    recv_head = (recv_head + 1) % kRQDepth;
    recvs_to_post = 0;  // Reset slack counter
    return;
  }

  // Nothing should be here
}
}  // namespace erpc

#endif
