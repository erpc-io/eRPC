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
      sgl[0].lkey = msg_buffer->buffer.lkey;

      // Only single-SGE work requests are inlined
      wr.send_flags |= (sgl[0].length <= kMaxInline) ? IBV_SEND_INLINE : 0;
      wr.num_sge = 1;
    } else {
      // This is not the first packet, so we need 2 SGEs. This involves a
      // a division, which is OK because it is a large message.
      pkthdr = msg_buffer->get_pkthdr_n(item.offset / kMaxDataPerPkt);
      sgl[0].addr = reinterpret_cast<uint64_t>(pkthdr);
      sgl[0].length = static_cast<uint32_t>(sizeof(pkthdr_t));
      sgl[0].lkey = msg_buffer->buffer.lkey;

      send_sgl[i][1].addr =
          reinterpret_cast<uint64_t>(&msg_buffer->buf[item.offset]);
      send_sgl[i][1].length = static_cast<uint32_t>(item.data_bytes);
      send_sgl[i][1].lkey = msg_buffer->buffer.lkey;

      wr.num_sge = 2;
    }

    const auto* raw_rinfo =
        reinterpret_cast<raw_routing_info_t*>(item.routing_info);
    _unused(raw_rinfo);
    // Fill L4 header
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

size_t RawTransport::rx_burst() { return 0; }

void RawTransport::post_recvs(size_t num_recvs) { _unused(num_recvs); }

}  // End erpc
