#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "ib_transport.h"
#include "util/udp_client.h"

namespace ERpc {

IBTransport::IBTransport(HugeAllocator *huge_alloc, uint8_t phy_port,
                         uint8_t app_tid)
    : Transport(TransportType::kInfiniBand, phy_port, huge_alloc, app_tid) {
  resolve_phy_port();
  init_infiniband_structs();

  erpc_dprintf("eRPC IBTransport: Created for TID %u. Device %s, port %d.\n",
               app_tid, ib_ctx->device->name, dev_port_id);
}

IBTransport::~IBTransport() {
  /* Do not destroy @huge_alloc; the parent Rpc will do it. */
}

void IBTransport::fill_routing_info(RoutingInfo *routing_info) const {
  memset((void *)routing_info, 0, kMaxRoutingInfoSize);
  ib_routing_info_t *ib_routing_info = (ib_routing_info_t *)routing_info;
  ib_routing_info->port_lid = port_lid;
  ib_routing_info->qpn = qp->qp_num;
}

void IBTransport::send_message(Session *session, const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

void IBTransport::poll_completions() {}

void IBTransport::resolve_phy_port() {
  std::ostringstream xmsg; /* The exception message */

  /* Get the device list */
  int num_devices = 0;
  struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
  if (dev_list == nullptr) {
    throw std::runtime_error(
        "eRPC IBTransport: Failed to get InfiniBand device list");
  }

  /* Traverse the device list */
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
      /* Count this port only if it is enabled */
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
        /* Resolution done. ib_ctx contains the resolved device context. */
        device_id = dev_i;
        dev_port_id = port_i;
        port_lid = port_attr.lid;
        return;
      }

      ports_to_discover--;
    }

    /* Thank you Mario, but our port is in another device */
    if (ibv_close_device(ib_ctx) != 0) {
      xmsg << "eRPC IBTransport: Failed to close InfiniBand device "
           << ib_ctx->device->name;
      throw std::runtime_error(xmsg.str());
    }
  }

  /* If we are here, port resolution has failed */
  assert(device_id == -1 && dev_port_id == -1);
  xmsg << "eRPC IBTransport: Failed to resolve InfiniBand port index "
       << std::to_string(phy_port);
  throw std::runtime_error(xmsg.str());
}

void IBTransport::init_infiniband_structs() {
  assert(ib_ctx != nullptr && device_id != -1 && dev_port_id != -1);

  /* Create protection domain, send CQ, and recv CQ */
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

  /* Initialize QP creation attributes */
  struct ibv_qp_init_attr create_attr;
  memset((void *)&create_attr, 0, sizeof(struct ibv_qp_init_attr));
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

  /* Transition QP to INIT state */
  struct ibv_qp_attr init_attr;
  memset((void *)&init_attr, 0, sizeof(struct ibv_qp_attr));
  init_attr.qp_state = IBV_QPS_INIT;
  init_attr.pkey_index = 0;
  init_attr.port_num = (uint8_t)dev_port_id;
  init_attr.qkey = kQKey;

  if (ibv_modify_qp(qp, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                        IBV_QP_PORT | IBV_QP_QKEY) != 0) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to init");
  }

  /* RTR state */
  struct ibv_qp_attr rtr_attr;
  memset((void *)&rtr_attr, 0, sizeof(struct ibv_qp_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;

  if (ibv_modify_qp(qp, &rtr_attr, IBV_QP_STATE)) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to RTR");
  }

  /* Reuse rtr_attr for RTS */
  rtr_attr.qp_state = IBV_QPS_RTS;
  rtr_attr.sq_psn = 0; /* PSN does not matter for UD QPs */

  if (ibv_modify_qp(qp, &rtr_attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
    throw std::runtime_error("eRPC IBTransport: Failed to modify QP to RTS");
  }
}

void IBTransport::init_recvs() {
  std::ostringstream xmsg; /* The exception message */

  /* Initialize the memory region for RECVs */
  size_t size = kRecvQueueDepth * kRecvSize;
  recv_extent = (uint8_t *)huge_alloc->alloc_huge(size);
  if (recv_extent == nullptr) {
    xmsg << "eRPC IBTransport: Failed to allocate " << std::setprecision(2)
         << (double)size / MB(1) << "MB for RECV buffers.";
    throw std::runtime_error(xmsg.str());
  }

  recv_extent_mr = ibv_reg_mr(pd, recv_extent, size, IBV_ACCESS_LOCAL_WRITE);
  if (recv_extent_mr == nullptr) {
    throw std::runtime_error(
        "eRPC IBTransport: Failed to register RECV memory region");
  }

  /* Initialize constant fields of RECV descriptors */
  for (size_t wr_i = 0; wr_i < kRecvQueueDepth; wr_i++) {
    recv_sgl[wr_i].length = kRecvSize;
    recv_sgl[wr_i].lkey = recv_extent_mr->lkey;
    recv_sgl[wr_i].addr = (uintptr_t)&recv_extent[wr_i * kRecvSize];

    recv_wr[wr_i].wr_id = recv_sgl[wr_i].addr; /* Debug */
    recv_wr[wr_i].sg_list = &recv_sgl[wr_i];
    recv_wr[wr_i].num_sge = 1;

    /* Circular link */
    recv_wr[wr_i].next =
        (wr_i < kRecvQueueDepth - 1) ? &recv_wr[wr_i + 1] : &recv_wr[0];
  }
}

void IBTransport::init_sends() {
  for (size_t wr_i = 0; wr_i < kPostlist; wr_i++) {
    send_sgl[wr_i].lkey = req_retrans_mr->lkey;

    send_wr[wr_i].next = &send_wr[wr_i + 1];
    send_wr[wr_i].wr.ud.remote_qkey = kQKey;
    send_wr[wr_i].opcode = IBV_WR_SEND_WITH_IMM;
    send_wr[wr_i].num_sge = 1;
    send_wr[wr_i].sg_list = &send_sgl[wr_i];
  }
}

}  // End ERpc
