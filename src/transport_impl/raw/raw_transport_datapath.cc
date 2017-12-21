#include "raw_transport.h"

namespace erpc {

// Packets that are the first packet in their MsgBuffer use one DMA, and may
// be inlined. Packets that are not the first packet use two DMAs, and are never
// inlined for simplicity.

void RawTransport::tx_burst(const tx_burst_item_t* tx_burst_arr,
                            size_t num_pkts) {
  for (size_t i = 0; i < num_pkts; i++) {
    const tx_burst_item_t& item = tx_burst_arr[i];
    const MsgBuffer* msg_buffer = item.msg_buffer;
    assert(msg_buffer->is_valid());  // Can be fake for control packets
    assert(item.data_bytes <= kMaxDataPerPkt);  // Can be 0 for control packets
    assert(item.offset + item.data_bytes <= msg_buffer->data_size);

    // Verify constant fields of work request
    struct ibv_send_wr& wr = send_wr[i];
    struct ibv_sge* sgl = send_sgl[i];

    assert(wr.next == &send_wr[i + 1]);  // +1 is valid
    assert(wr.opcode == IBV_WR_SEND);
    assert(wr.sg_list == sgl);

    // Set signaling flag. The work request is non-inline by default.
    wr.send_flags = get_signaled_flag();

    pkthdr_t* pkthdr;
    if (item.offset == 0) {
      // This is the first packet, so we need only 1 SGE. This can be a credit
      // return packet or an RFR.
      pkthdr = msg_buffer->get_pkthdr_0();
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t) + item.data_bytes);
      assert(sgl[0].lkey == 0);

      if (LOG_LEVEL == LOG_LEVEL_TRACE) {
        // Print out the Ethernet frame
        printf(
            "eRPC RawTransport: Sending message in one Ethernet frame. "
            "Size = %u bytes, contents = \n",
            sgl[0].length);
        for (size_t i = 0; i < sgl[0].length; i++) {
          printf("%u ", (reinterpret_cast<uint8_t*>(pkthdr))[i]);
        }
        printf("\n");
      }

      // Only single-SGE work requests are inlined
      wr.send_flags |= (sgl[0].length <= kMaxInline) ? IBV_SEND_INLINE : 0;
      wr.num_sge = 1;
    } else {
      // This is not the first packet, so we need 2 SGEs. This involves a
      // a division, which is OK because it is a large message.
      pkthdr = msg_buffer->get_pkthdr_n(item.offset / kMaxDataPerPkt);
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t));
      assert(sgl[0].lkey == 0);

      sgl[1].addr = reinterpret_cast<uint64_t>(&msg_buffer->buf[item.offset]);
      sgl[1].length = static_cast<uint32_t>(item.data_bytes);
      assert(sgl[1].lkey == 0);

      wr.num_sge = 2;
    }

    const auto* raw_rinfo =
        reinterpret_cast<raw_routing_info_t*>(item.routing_info);

    auto* eth_hdr = reinterpret_cast<eth_hdr_t*>(&pkthdr->headroom[0]);
    gen_eth_header(eth_hdr, &resolve.mac_addr[0], &raw_rinfo->mac[0]);

    const size_t ipv4_sz = sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) +
                           kERpcHdrBytes + item.data_bytes;
    auto* ipv4_hdr = reinterpret_cast<ipv4_hdr_t*>(&eth_hdr[1]);
    gen_ipv4_header(ipv4_hdr, resolve.ipv4_addr, raw_rinfo->ipv4_addr, ipv4_sz);

    const size_t udp_sz = ipv4_sz - sizeof(ipv4_hdr_t);
    auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);
    gen_udp_header(udp_hdr, kBaseRawUDPPort + rpc_id, raw_rinfo->udp_port,
                   udp_sz);
  }

  send_wr[num_pkts - 1].next = nullptr;  // Breaker of chains

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(send_qp, &send_wr[0], &bad_wr);
  assert(ret == 0);
  if (unlikely(ret != 0)) {
    fprintf(stderr, "eRPC: Fatal error. ibv_post_send failed. ret = %d\n", ret);
    exit(-1);
  }

  send_wr[num_pkts - 1].next = &send_wr[num_pkts];  // Restore chain; safe
}

void RawTransport::tx_flush() {}

size_t RawTransport::rx_burst() {
  cqe_snapshot_t cur_snapshot;
  snapshot_cqe(&recv_cqe_arr[cqe_idx], cur_snapshot);
  cqe_idx = (cqe_idx + 1) % kRecvCQDepth;
  prev_snapshot = cur_snapshot;
  return get_cqe_cycle_delta(prev_snapshot, cur_snapshot);
}

void RawTransport::post_recvs(size_t num_recvs) {
  recvs_to_post += num_recvs;
  if (recvs_to_post < kStridesPerWQE) return;

  int ret = wq_family->recv_burst(wq, &mp_recv_sge[mp_sge_idx], 1);
  assert(ret == 0);
  mp_sge_idx = (mp_sge_idx + 1) % kRQDepth;
}

}  // End erpc
