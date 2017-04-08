#include <iomanip>
#include <stdexcept>

#include "ib_transport.h"
#include "util/huge_alloc.h"
#include "util/udp_client.h"

namespace ERpc {

const size_t IBTransport::kMaxDataPerPkt;

// Initialize the protection domain, queue pair, and memory registration and
// deregistration functions. RECVs will be initialized later when the hugepage
// allocator is provided.
IBTransport::IBTransport(uint8_t rpc_id, uint8_t phy_port)
    : Transport(TransportType::kInfiniBand, rpc_id, phy_port) {
  resolve_phy_port();
  init_infiniband_structs();
  init_mem_reg_funcs();

  erpc_dprintf("eRPC IBTransport: Created for ID %u. Device %s, port %d.\n",
               rpc_id, ib_ctx->device->name, dev_port_id);
}

void IBTransport::init_hugepage_structures(HugeAlloc *huge_alloc,
                                           uint8_t **rx_ring) {
  assert(huge_alloc != nullptr);
  assert(rx_ring != nullptr);

  this->huge_alloc = huge_alloc;
  this->numa_node = huge_alloc->get_numa_node();

  init_recvs(rx_ring);
  init_sends();
}

// The transport destructore is called after \p huge_alloc has already been
// destroyed by \p Rpc. Deleting \p huge_alloc deregisters and frees all SHM
// memory regions.
//
// We only need to clean up non-hugepage structures.
IBTransport::~IBTransport() {
  erpc_dprintf("eRPC IBTransport: Destroying transport for ID %u\n", rpc_id);

  // Destroy QPs and CQs. QPs must be destroyed before CQs.
  if (ibv_destroy_qp(qp)) {
    throw std::runtime_error("eRPC IBTransport: Failed to destroy QP.");
  }

  if (ibv_destroy_cq(send_cq)) {
    throw std::runtime_error("eRPC IBTransport: Failed to destroy send CQ.");
  }

  if (ibv_destroy_cq(recv_cq)) {
    throw std::runtime_error("eRPC IBTransport: Failed to destroy recv CQ.");
  }

  // Destroy protection domain and device context
  if (ibv_dealloc_pd(pd)) {
    throw std::runtime_error(
        "eRPC IBTransport: Failed to deallocate protection domain.");
  }

  if (ibv_close_device(ib_ctx)) {
    throw std::runtime_error("eRPC IBTransport: Failed to close device.");
  }
}

void IBTransport::fill_local_routing_info(RoutingInfo *routing_info) const {
  assert(routing_info != nullptr);
  memset(static_cast<void *>(routing_info), 0, kMaxRoutingInfoSize);
  ib_routing_info_t *ib_routing_info =
      reinterpret_cast<ib_routing_info_t *>(routing_info);
  ib_routing_info->port_lid = port_lid;
  ib_routing_info->qpn = qp->qp_num;
}

bool IBTransport::resolve_remote_routing_info(RoutingInfo *routing_info) const {
  assert(routing_info != nullptr);
  assert(dev_port_id != 0);  // dev_port_id is resolved on object construction

  ib_routing_info_t *ib_routing_info =
      reinterpret_cast<ib_routing_info_t *>(routing_info);

  struct ibv_ah_attr ah_attr;
  memset(&ah_attr, 0, sizeof(struct ibv_ah_attr));
  ah_attr.is_global = 0;
  ah_attr.dlid = ib_routing_info->port_lid;
  ah_attr.sl = 0;
  ah_attr.src_path_bits = 0;

  ah_attr.port_num = dev_port_id;  // Local port
  ib_routing_info->ah = ibv_create_ah(pd, &ah_attr);

  return (ib_routing_info->ah != nullptr);
}

void IBTransport::resolve_phy_port() {
  std::ostringstream xmsg;  // The exception message

  // Get the device list
  int num_devices = 0;
  struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
  if (dev_list == nullptr) {
    throw std::runtime_error(
        "eRPC IBTransport: Failed to get InfiniBand device list");
  }

  // Traverse the device list
  int ports_to_discover = phy_port;

  for (int dev_i = 0; dev_i < num_devices; dev_i++) {
    ib_ctx = ibv_open_device(dev_list[dev_i]);
    if (ib_ctx == nullptr) {
      xmsg << "eRPC IBTransport: Failed to open InfiniBand device "
           << std::to_string(dev_i);
      throw std::runtime_error(xmsg.str());
    }

    struct ibv_device_attr device_attr;
    memset(&device_attr, 0, sizeof(device_attr));
    if (ibv_query_device(ib_ctx, &device_attr) != 0) {
      xmsg << "eRPC IBTransport: Failed to query InfiniBand device "
           << std::to_string(dev_i);
      throw std::runtime_error(xmsg.str());
    }

    for (uint8_t port_i = 1; port_i <= device_attr.phys_port_cnt; port_i++) {
      // Count this port only if it is enabled
      struct ibv_port_attr port_attr;
      if (ibv_query_port(ib_ctx, port_i, &port_attr) != 0) {
        xmsg << "eRPC IBTransport: Failed to query port "
             << std::to_string(port_i) << " on device " << ib_ctx->device->name;
        throw std::runtime_error(xmsg.str());
      }

      if (port_attr.phys_state != IBV_PORT_ACTIVE &&
          port_attr.phys_state != IBV_PORT_ACTIVE_DEFER) {
        continue;
      }

      if (ports_to_discover == 0) {
        // Resolution done. ib_ctx contains the resolved device context.
        device_id = dev_i;
        dev_port_id = port_i;
        port_lid = port_attr.lid;
        return;
      }

      ports_to_discover--;
    }

    // Thank you Mario, but our port is in another device
    if (ibv_close_device(ib_ctx) != 0) {
      xmsg << "eRPC IBTransport: Failed to close InfiniBand device "
           << ib_ctx->device->name;
      throw std::runtime_error(xmsg.str());
    }
  }

  // If we are here, port resolution has failed
  assert(device_id == -1 && dev_port_id == 0);
  xmsg << "eRPC IBTransport: Failed to resolve InfiniBand port index "
       << std::to_string(phy_port);
  throw std::runtime_error(xmsg.str());
}

void IBTransport::init_infiniband_structs() {
  assert(ib_ctx != nullptr && device_id != -1);

  // Create protection domain, send CQ, and recv CQ
  pd = ibv_alloc_pd(ib_ctx);
  if (pd == nullptr) {
    throw std::runtime_error(
        "eRPC IBTransport: Failed to create protection domain");
  }

  send_cq = ibv_create_cq(ib_ctx, kSendQueueDepth, nullptr, nullptr, 0);
  if (send_cq == nullptr) {
    throw std::runtime_error("eRPC IBTransport: Failed to create SEND CQ");
  }

  recv_cq = ibv_create_cq(ib_ctx, kRecvQueueDepth, nullptr, nullptr, 0);
  if (recv_cq == nullptr) {
    throw std::runtime_error("eRPC IBTransport: Failed to create SEND CQ");
  }

  // Initialize QP creation attributes
  struct ibv_qp_init_attr create_attr;
  memset(static_cast<void *>(&create_attr), 0, sizeof(struct ibv_qp_init_attr));
  create_attr.send_cq = send_cq;
  create_attr.recv_cq = recv_cq;
  create_attr.qp_type = IBV_QPT_UD;

  create_attr.cap.max_send_wr = kSendQueueDepth;
  create_attr.cap.max_recv_wr = kRecvQueueDepth;
  create_attr.cap.max_send_sge = 1;
  create_attr.cap.max_recv_sge = 1;
  create_attr.cap.max_inline_data = kMaxInline;

  qp = ibv_create_qp(pd, &create_attr);
  if (qp == nullptr) {
    throw std::runtime_error("eRPC IBTransport: Failed to create QP");
  }

  // Transition QP to INIT state
  struct ibv_qp_attr init_attr;
  memset(static_cast<void *>(&init_attr), 0, sizeof(struct ibv_qp_attr));
  init_attr.qp_state = IBV_QPS_INIT;
  init_attr.pkey_index = 0;
  init_attr.port_num = static_cast<uint8_t>(dev_port_id);
  init_attr.qkey = kQKey;

  if (ibv_modify_qp(qp, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                        IBV_QP_PORT | IBV_QP_QKEY) != 0) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to init");
  }

  // RTR state
  struct ibv_qp_attr rtr_attr;
  memset(static_cast<void *>(&rtr_attr), 0, sizeof(struct ibv_qp_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;

  if (ibv_modify_qp(qp, &rtr_attr, IBV_QP_STATE)) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to RTR");
  }

  // Reuse rtr_attr for RTS
  rtr_attr.qp_state = IBV_QPS_RTS;
  rtr_attr.sq_psn = 0;  // PSN does not matter for UD QPs

  if (ibv_modify_qp(qp, &rtr_attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to RTS");
  }
}

void IBTransport::init_mem_reg_funcs() {
  using namespace std::placeholders;
  assert(pd != nullptr);
  reg_mr_func = std::bind(ibv_reg_mr_wrapper, pd, _1, _2);
  dereg_mr_func = std::bind(ibv_dereg_mr_wrapper, _1);
}

void IBTransport::init_recvs(uint8_t **rx_ring) {
  assert(rx_ring != nullptr);

  std::ostringstream xmsg;  // The exception message

  // Initialize the memory region for RECVs
  size_t recv_extent_size = kRecvQueueDepth * kRecvSize;
  recv_extent = huge_alloc->alloc(recv_extent_size);
  if (recv_extent.buf == nullptr) {
    xmsg << "eRPC IBTransport: Failed to allocate " << std::setprecision(2)
         << 1.0 * recv_extent_size / MB(1) << "MB for RECV buffers.";
    throw std::runtime_error(xmsg.str());
  }

  // Initialize constant fields of RECV descriptors
  for (size_t i = 0; i < kRecvQueueDepth; i++) {
    uint8_t *buf = recv_extent.buf;

    // From each slot of size kRecvSize = (kMTU + 64), we give up the first
    // (64 - kGRHBytes) bytes. Each slot is still large enough to receive the
    // GRH and kMTU payload bytes.
    size_t offset = (i * kRecvSize) + (64 - kGRHBytes);
    assert(offset + (kGRHBytes + kMTU) <= recv_extent_size);

    recv_sgl[i].length = kRecvSize;
    recv_sgl[i].lkey = recv_extent.lkey;
    recv_sgl[i].addr = reinterpret_cast<uint64_t>(&buf[offset]);

    recv_wr[i].wr_id = recv_sgl[i].addr;  // Debug
    recv_wr[i].sg_list = &recv_sgl[i];
    recv_wr[i].num_sge = 1;

    // Circular link
    recv_wr[i].next = (i < kRecvQueueDepth - 1) ? &recv_wr[i + 1] : &recv_wr[0];

    // RX ring entry
    rx_ring[i] = &buf[offset + kGRHBytes];
  }

  // Fill the RECV queue
  post_recvs(kRecvQueueDepth);
}

void IBTransport::init_sends() {
  for (size_t i = 0; i < kPostlist; i++) {
    send_wr[i].next = &send_wr[i + 1];
    send_wr[i].wr.ud.remote_qkey = kQKey;
    send_wr[i].opcode = IBV_WR_SEND_WITH_IMM;
    send_wr[i].sg_list = &send_sgl[i][0];
  }
}

}  // End ERpc
