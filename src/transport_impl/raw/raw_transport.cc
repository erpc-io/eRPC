#include <iomanip>
#include <stdexcept>

#include "raw_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

constexpr size_t RawTransport::kMaxDataPerPkt;

// Initialize the protection domain, queue pair, and memory registration and
// deregistration functions. RECVs will be initialized later when the hugepage
// allocator is provided.
RawTransport::RawTransport(uint8_t rpc_id, uint8_t phy_port, size_t numa_node)
    : Transport(TransportType::kRaw, rpc_id, phy_port, numa_node),
      rx_flow_udp_port(kBaseRawUDPPort + (256u * numa_node) + rpc_id) {
  rt_assert(kHeadroom == 40, "Invalid packet header headroom for raw Ethernet");
  rt_assert(sizeof(pkthdr_t::headroom) == kInetHdrsTotSize, "Invalid headroom");

  resolve_phy_port();
  init_verbs_structs();
  init_mem_reg_funcs();

  LOG_WARN(
      "Created for ID %u. Device (%s, %s). IPv4 %s, MAC %s. "
      "port %d.\n",
      rpc_id, resolve.ibdev_name.c_str(), resolve.netdev_name.c_str(),
      ipv4_to_string(resolve.ipv4_addr).c_str(),
      mac_to_string(resolve.mac_addr).c_str(), resolve.dev_port_id);
}

void RawTransport::init_hugepage_structures(HugeAlloc *huge_alloc,
                                            uint8_t **rx_ring) {
  this->huge_alloc = huge_alloc;

  init_recvs(rx_ring);
  init_sends();
}

// The transport destructor is called after \p huge_alloc has already been
// destroyed by \p Rpc. Deleting \p huge_alloc deregisters and frees all SHM
// memory regions.
//
// We only need to clean up non-hugepage structures.
RawTransport::~RawTransport() {
  LOG_INFO("Destroying transport for ID %u\n", rpc_id);

  if (kDumb) {
    exit_assert(ibv_destroy_qp(qp) == 0, "Failed to destroy QP.");
    exit_assert(ibv_destroy_cq(send_cq) == 0, "Failed to destroy send CQ.");

    struct ibv_exp_release_intf_params rel_intf_params;
    memset(&rel_intf_params, 0, sizeof(rel_intf_params));
    exit_assert(
        ibv_exp_release_intf(resolve.ib_ctx, wq_family, &rel_intf_params) == 0,
        "Failed to release interface.");

    exit_assert(ibv_exp_destroy_flow(recv_flow) == 0,
                "Failed to destroy RECV flow");

    exit_assert(ibv_destroy_qp(mp_recv_qp) == 0,
                "Failed to destroy MP RECV QP.");

    exit_assert(ibv_exp_destroy_rwq_ind_table(ind_tbl) == 0,
                "Failed to destroy indirection table.");

    exit_assert(ibv_exp_destroy_wq(wq) == 0, "Failed to destroy WQ.");
  } else {
    exit_assert(ibv_exp_destroy_flow(recv_flow) == 0,
                "Failed to destroy RECV flow");

    exit_assert(ibv_destroy_qp(qp) == 0, "Failed to destroy QP.");
    exit_assert(ibv_destroy_cq(send_cq) == 0, "Failed to destroy send CQ.");
  }

  exit_assert(ibv_destroy_cq(recv_cq) == 0, "Failed to destroy RECV CQ.");
  exit_assert(ibv_dealloc_pd(pd) == 0, "Failed to destroy protection domain.");
  exit_assert(ibv_close_device(resolve.ib_ctx) == 0, "Failed to close device.");
}

void RawTransport::fill_local_routing_info(RoutingInfo *routing_info) const {
  memset(static_cast<void *>(routing_info), 0, kMaxRoutingInfoSize);
  auto *ri = reinterpret_cast<raw_routing_info_t *>(routing_info);
  memcpy(ri->mac, resolve.mac_addr, 6);
  ri->ipv4_addr = resolve.ipv4_addr;
  ri->udp_port = rx_flow_udp_port;
}

// Generate most fields of the L2--L4 headers now to avoid recomputation.
bool RawTransport::resolve_remote_routing_info(
    RoutingInfo *routing_info) const {
  auto *ri = reinterpret_cast<raw_routing_info_t *>(routing_info);
  uint8_t remote_mac[6];
  memcpy(remote_mac, ri->mac, 6);
  uint32_t remote_ipv4_addr = ri->ipv4_addr;
  uint16_t remote_udp_port = ri->udp_port;

  static_assert(kMaxRoutingInfoSize >= kInetHdrsTotSize, "");

  auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(ri);
  gen_eth_header(eth_hdr, &resolve.mac_addr[0], remote_mac);

  auto *ipv4_hdr = reinterpret_cast<ipv4_hdr_t *>(&eth_hdr[1]);
  gen_ipv4_header(ipv4_hdr, resolve.ipv4_addr, remote_ipv4_addr, 0);

  auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(&ipv4_hdr[1]);
  gen_udp_header(udp_hdr, rx_flow_udp_port, remote_udp_port, 0);
  return true;
}

void RawTransport::resolve_phy_port() {
  std::ostringstream xmsg;  // The exception message

  // Get the device list
  int num_devices = 0;
  struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
  rt_assert(dev_list != nullptr, "Failed to get device list");

  // Traverse the device list
  int ports_to_discover = phy_port;

  for (int dev_i = 0; dev_i < num_devices; dev_i++) {
    struct ibv_context *ib_ctx = ibv_open_device(dev_list[dev_i]);
    rt_assert(ib_ctx != nullptr, "Failed to open dev " + std::to_string(dev_i));

    struct ibv_device_attr device_attr;
    memset(&device_attr, 0, sizeof(device_attr));
    if (ibv_query_device(ib_ctx, &device_attr) != 0) {
      xmsg << "Failed to query device " << std::to_string(dev_i);
      throw std::runtime_error(xmsg.str());
    }

    for (uint8_t port_i = 1; port_i <= device_attr.phys_port_cnt; port_i++) {
      // Count this port only if it is enabled
      struct ibv_port_attr port_attr;
      if (ibv_query_port(ib_ctx, port_i, &port_attr) != 0) {
        xmsg << "Failed to query port " << std::to_string(port_i)
             << " on device " << ib_ctx->device->name;
        throw std::runtime_error(xmsg.str());
      }

      if (port_attr.phys_state != IBV_PORT_ACTIVE &&
          port_attr.phys_state != IBV_PORT_ACTIVE_DEFER) {
        continue;
      }

      if (ports_to_discover == 0) {
        // Resolution succeeded. Check if the link layer matches.
        if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET) {
          throw std::runtime_error(
              "Transport type required is raw Ethernet but port L2 is " +
              link_layer_str(port_attr.link_layer));
        }

        // Check the class's constant MTU
        size_t active_mtu = enum_to_mtu(port_attr.active_mtu);
        if (kMTU > active_mtu) {
          throw std::runtime_error("Transport's required MTU is " +
                                   std::to_string(kMTU) + ", active_mtu is " +
                                   std::to_string(active_mtu));
        }

        LOG_INFO("Port %u resolved to device %s, port %u\n", phy_port,
                 ib_ctx->device->name, port_i);

        resolve.device_id = dev_i;
        resolve.ib_ctx = ib_ctx;
        resolve.dev_port_id = port_i;

        resolve.ibdev_name = std::string(ib_ctx->device->name);
        resolve.netdev_name = ibdev2netdev(resolve.ibdev_name);
        resolve.ipv4_addr = get_interface_ipv4_addr(resolve.netdev_name);
        fill_interface_mac(resolve.netdev_name, resolve.mac_addr);

        return;
      }

      ports_to_discover--;
    }

    // Thank you Mario, but our port is in another device
    if (ibv_close_device(ib_ctx) != 0) {
      xmsg << "Failed to close device " << ib_ctx->device->name;
      throw std::runtime_error(xmsg.str());
    }
  }

  // If we are here, port resolution has failed
  assert(resolve.ib_ctx == nullptr);
  xmsg << "Failed to resolve RoCE port index " << std::to_string(phy_port);
  throw std::runtime_error(xmsg.str());
}

/// Initialize QPs used for SENDs only
void RawTransport::init_send_qp() {
  assert(resolve.ib_ctx != nullptr && pd != nullptr);

  struct ibv_exp_cq_init_attr cq_init_attr;
  memset(&cq_init_attr, 0, sizeof(cq_init_attr));
  send_cq = ibv_exp_create_cq(resolve.ib_ctx, kSQDepth, nullptr, nullptr, 0,
                              &cq_init_attr);
  rt_assert(send_cq != nullptr, "Failed to create SEND CQ. Forgot hugepages?");

  // In dumbpipe mode, we don't need a RECV CQ for this QP
  if (!kDumb) {
    recv_cq = ibv_exp_create_cq(resolve.ib_ctx, kRQDepth, nullptr, nullptr, 0,
                                &cq_init_attr);
    rt_assert(send_cq != nullptr, "Failed to create RECV CQ");
  }

  struct ibv_exp_qp_init_attr qp_init_attr;
  memset(&qp_init_attr, 0, sizeof(qp_init_attr));
  qp_init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;

  qp_init_attr.pd = pd;
  qp_init_attr.send_cq = send_cq;
  qp_init_attr.recv_cq = kDumb ? send_cq : recv_cq;  // recv_cq comment above
  qp_init_attr.cap.max_send_wr = kSQDepth;
  qp_init_attr.cap.max_send_sge = 0;  // This is better than 2!?
  qp_init_attr.cap.max_recv_wr = kDumb ? 0 : kRQDepth;
  qp_init_attr.cap.max_recv_sge = kDumb ? 0 : 1;
  qp_init_attr.cap.max_inline_data = kMaxInline;
  qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;

  qp = ibv_exp_create_qp(resolve.ib_ctx, &qp_init_attr);
  rt_assert(qp != nullptr, "Failed to create QP");

  struct ibv_exp_qp_attr qp_attr;
  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_INIT;
  qp_attr.port_num = 1;
  rt_assert(ibv_exp_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PORT) == 0);

  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTR;
  rt_assert(ibv_exp_modify_qp(qp, &qp_attr, IBV_QP_STATE) == 0);

  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTS;
  rt_assert(ibv_exp_modify_qp(qp, &qp_attr, IBV_QP_STATE) == 0);
}

/// Initialize a multi-packet RECV QP
void RawTransport::init_mp_recv_qp() {
  assert(kDumb && resolve.ib_ctx != nullptr && pd != nullptr);

  // Init CQ. Its size MUST be one so that we get two CQEs in mlx5.
  struct ibv_exp_cq_init_attr cq_init_attr;
  memset(&cq_init_attr, 0, sizeof(cq_init_attr));
  recv_cq = ibv_exp_create_cq(resolve.ib_ctx, kRecvCQDepth / 2, nullptr,
                              nullptr, 0, &cq_init_attr);
  rt_assert(recv_cq != nullptr, "Failed to create RECV CQ");

  // Modify the RECV CQ to ignore overrun
  struct ibv_exp_cq_attr cq_attr;
  memset(&cq_attr, 0, sizeof(cq_attr));
  cq_attr.comp_mask = IBV_EXP_CQ_ATTR_CQ_CAP_FLAGS;
  cq_attr.cq_cap_flags = IBV_EXP_CQ_IGNORE_OVERRUN;
  rt_assert(ibv_exp_modify_cq(recv_cq, &cq_attr, IBV_EXP_CQ_CAP_FLAGS) == 0);

  struct ibv_exp_wq_init_attr wq_init_attr;
  memset(&wq_init_attr, 0, sizeof(wq_init_attr));

  wq_init_attr.wq_type = IBV_EXP_WQT_RQ;
  wq_init_attr.max_recv_wr = kRQDepth;
  wq_init_attr.max_recv_sge = 1;
  wq_init_attr.pd = pd;
  wq_init_attr.cq = recv_cq;

  wq_init_attr.comp_mask |= IBV_EXP_CREATE_WQ_MP_RQ;
  wq_init_attr.mp_rq.use_shift = IBV_EXP_MP_RQ_NO_SHIFT;
  wq_init_attr.mp_rq.single_wqe_log_num_of_strides = kLogNumStrides;
  wq_init_attr.mp_rq.single_stride_log_num_of_bytes = kLogStrideBytes;
  wq = ibv_exp_create_wq(resolve.ib_ctx, &wq_init_attr);
  rt_assert(wq != nullptr, "Failed to create WQ");

  // Change WQ to ready state
  struct ibv_exp_wq_attr wq_attr;
  memset(&wq_attr, 0, sizeof(wq_attr));
  wq_attr.attr_mask = IBV_EXP_WQ_ATTR_STATE;
  wq_attr.wq_state = IBV_EXP_WQS_RDY;
  rt_assert(ibv_exp_modify_wq(wq, &wq_attr) == 0, "Failed to ready WQ");

  // Get the RQ burst function
  enum ibv_exp_query_intf_status intf_status = IBV_EXP_INTF_STAT_OK;
  struct ibv_exp_query_intf_params query_intf_params;
  memset(&query_intf_params, 0, sizeof(query_intf_params));
  query_intf_params.intf_scope = IBV_EXP_INTF_GLOBAL;
  query_intf_params.intf = IBV_EXP_INTF_WQ;
  query_intf_params.obj = wq;
  wq_family = reinterpret_cast<struct ibv_exp_wq_family *>(
      ibv_exp_query_intf(resolve.ib_ctx, &query_intf_params, &intf_status));
  rt_assert(wq_family != nullptr, "Failed to get WQ interface");

  // Create indirect table
  struct ibv_exp_rwq_ind_table_init_attr rwq_ind_table_init_attr;
  memset(&rwq_ind_table_init_attr, 0, sizeof(rwq_ind_table_init_attr));
  rwq_ind_table_init_attr.pd = pd;
  rwq_ind_table_init_attr.log_ind_tbl_size = 0;  // Ignore hash
  rwq_ind_table_init_attr.ind_tbl = &wq;         // Pointer to RECV work queue
  rwq_ind_table_init_attr.comp_mask = 0;
  ind_tbl =
      ibv_exp_create_rwq_ind_table(resolve.ib_ctx, &rwq_ind_table_init_attr);
  rt_assert(ind_tbl != nullptr, "Failed to create indirection table");

  // Create rx_hash_conf and indirection table for the QP
  uint8_t toeplitz_key[] = {0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
                            0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
                            0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
                            0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
                            0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa};
  const int TOEPLITZ_RX_HASH_KEY_LEN =
      sizeof(toeplitz_key) / sizeof(toeplitz_key[0]);

  struct ibv_exp_rx_hash_conf rx_hash_conf;
  memset(&rx_hash_conf, 0, sizeof(rx_hash_conf));
  rx_hash_conf.rx_hash_function = IBV_EXP_RX_HASH_FUNC_TOEPLITZ;
  rx_hash_conf.rx_hash_key_len = TOEPLITZ_RX_HASH_KEY_LEN;
  rx_hash_conf.rx_hash_key = toeplitz_key;
  rx_hash_conf.rx_hash_fields_mask = IBV_EXP_RX_HASH_DST_PORT_UDP;
  rx_hash_conf.rwq_ind_tbl = ind_tbl;

  struct ibv_exp_qp_init_attr qp_init_attr;
  memset(&qp_init_attr, 0, sizeof(qp_init_attr));
  qp_init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS |
                           IBV_EXP_QP_INIT_ATTR_PD |
                           IBV_EXP_QP_INIT_ATTR_RX_HASH;
  qp_init_attr.rx_hash_conf = &rx_hash_conf;
  qp_init_attr.pd = pd;
  qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;

  // Create the QP
  mp_recv_qp = ibv_exp_create_qp(resolve.ib_ctx, &qp_init_attr);
  rt_assert(mp_recv_qp != nullptr, "Failed to create RECV QP");
}

void RawTransport::install_flow_rule() {
  struct ibv_qp *qp_for_flow = kDumb ? mp_recv_qp : qp;
  assert(qp_for_flow != nullptr);

  LOG_WARN(
      "Installing flow rule for Rpc %u. NUMA node = %zu. "
      "Flow RX UDP port = %u.\n",
      rpc_id, numa_node, rx_flow_udp_port);

  static constexpr size_t rule_sz =
      sizeof(ibv_exp_flow_attr) + sizeof(ibv_exp_flow_spec_eth) +
      sizeof(ibv_exp_flow_spec_ipv4_ext) + sizeof(ibv_exp_flow_spec_tcp_udp);

  uint8_t *flow_rule = new uint8_t[rule_sz];
  memset(flow_rule, 0, rule_sz);
  uint8_t *buf = flow_rule;

  auto *flow_attr = reinterpret_cast<struct ibv_exp_flow_attr *>(flow_rule);
  flow_attr->type = IBV_EXP_FLOW_ATTR_NORMAL;
  flow_attr->size = rule_sz;
  flow_attr->priority = 0;
  flow_attr->num_of_specs = 3;
  flow_attr->port = 1;
  flow_attr->flags = 0;
  flow_attr->reserved = 0;
  buf += sizeof(struct ibv_exp_flow_attr);

  // Ethernet - all wildcard
  auto *eth_spec = reinterpret_cast<struct ibv_exp_flow_spec_eth *>(buf);
  eth_spec->type = IBV_EXP_FLOW_SPEC_ETH;
  eth_spec->size = sizeof(struct ibv_exp_flow_spec_eth);
  buf += sizeof(struct ibv_exp_flow_spec_eth);

  // IPv4 - all wildcard
  auto *spec_ipv4 = reinterpret_cast<struct ibv_exp_flow_spec_ipv4_ext *>(buf);
  spec_ipv4->type = IBV_EXP_FLOW_SPEC_IPV4_EXT;
  spec_ipv4->size = sizeof(struct ibv_exp_flow_spec_ipv4_ext);
  buf += sizeof(struct ibv_exp_flow_spec_ipv4_ext);

  // UDP - match dst port
  auto *udp_spec = reinterpret_cast<struct ibv_exp_flow_spec_tcp_udp *>(buf);
  udp_spec->type = IBV_EXP_FLOW_SPEC_UDP;
  udp_spec->size = sizeof(struct ibv_exp_flow_spec_tcp_udp);
  udp_spec->val.dst_port = htons(rx_flow_udp_port);
  udp_spec->mask.dst_port = 0xffffu;

  recv_flow = ibv_exp_create_flow(qp_for_flow, flow_attr);
  rt_assert(recv_flow != nullptr, "Failed to create RECV flow");
}

void RawTransport::map_mlx5_overrunning_recv_cqes() {
  assert(kDumb);

  // This cast works for mlx5 where ibv_cq is the first member of mlx5_cq.
  auto *_mlx5_cq = reinterpret_cast<mlx5_cq *>(recv_cq);
  rt_assert(kRecvCQDepth == std::pow(2, _mlx5_cq->cq_log_size),
            "mlx5 CQ depth does not match kRecvCQDepth");
  rt_assert(_mlx5_cq->buf_a.buf != nullptr);

  recv_cqe_arr = reinterpret_cast<volatile mlx5_cqe64 *>(_mlx5_cq->buf_a.buf);

  // Initialize the CQEs as if we received the last (kRecvCQDepth) packets in
  // the CQE cycle.
  rt_assert(kStridesPerWQE >= kRecvCQDepth, "");
  for (size_t i = 0; i < kRecvCQDepth; i++) {
    recv_cqe_arr[i].wqe_id = htons(UINT16_MAX);

    // Last CQE gets
    // * wqe_counter = (kAppStridesPerWQE - 1)
    // * snapshot_cycle_idx = (kAppCQESnapshotCycle - 1)
    recv_cqe_arr[i].wqe_counter = htons(kStridesPerWQE - (kRecvCQDepth - i));

    cqe_snapshot_t snapshot;
    snapshot_cqe(&recv_cqe_arr[i], snapshot);
    rt_assert(snapshot.get_cqe_snapshot_cycle_idx() ==
              kCQESnapshotCycle - (kRecvCQDepth - i));
  }

  snapshot_cqe(&recv_cqe_arr[kRecvCQDepth - 1], prev_snapshot);
}

void RawTransport::init_verbs_structs() {
  assert(resolve.ib_ctx != nullptr && resolve.device_id != -1);

  // Create protection domain, send CQ, and recv CQ
  pd = ibv_alloc_pd(resolve.ib_ctx);
  rt_assert(pd != nullptr, "Failed to allocate PD");

  init_send_qp();
  if (kDumb) init_mp_recv_qp();
  install_flow_rule();
  if (kDumb) map_mlx5_overrunning_recv_cqes();
}

void RawTransport::init_mem_reg_funcs() {
  using namespace std::placeholders;
  assert(pd != nullptr);
  reg_mr_func = std::bind(ibv_reg_mr_wrapper, pd, _1, _2);
  dereg_mr_func = std::bind(ibv_dereg_mr_wrapper, _1);
}

void RawTransport::init_recvs(uint8_t **rx_ring) {
  // In the dumbpipe mode, this function must be called only after mapping and
  // initializing the RECV CQEs. The NIC can DMA as soon as we post RECVs.
  if (kDumb) assert(recv_cqe_arr != nullptr);

  std::ostringstream xmsg;  // The exception message

  // Initialize the memory region for RECVs
  ring_extent = huge_alloc->alloc_raw(kRingSize, DoRegister::kTrue);
  if (ring_extent.buf == nullptr) {
    xmsg << "Failed to allocate " << std::setprecision(2)
         << 1.0 * kRingSize / MB(1) << "MB for ring buffers.";
    throw std::runtime_error(xmsg.str());
  }

  // Fill in the Rpc's RX ring
  for (size_t i = 0; i < kNumRxRingEntries; i++) {
    rx_ring[i] = &ring_extent.buf[kRecvSize * i];
  }

  // Initialize constant fields of multi-packet RECV SGEs and fill the RQ
  if (kDumb) {
    // In dumbpipe mode, we initialize SGEs, not RECV wr's
    for (size_t i = 0; i < kRQDepth; i++) {
      size_t mpwqe_offset = i * (kRecvSize * kStridesPerWQE);
      mp_recv_sge[i].addr =
          reinterpret_cast<uint64_t>(&ring_extent.buf[mpwqe_offset]);
      mp_recv_sge[i].lkey = ring_extent.lkey;
      mp_recv_sge[i].length = (kRecvSize * kStridesPerWQE);
      wq_family->recv_burst(wq, &mp_recv_sge[i], 1);
    }
  } else {
    for (size_t i = 0; i < kRQDepth; i++) {
      recv_sgl[i].length = kRecvSize;
      recv_sgl[i].lkey = ring_extent.lkey;
      recv_sgl[i].addr =
          reinterpret_cast<uint64_t>(&ring_extent.buf[i * kRecvSize]);

      recv_wr[i].wr_id = recv_sgl[i].addr;  // For quick prefetch
      recv_wr[i].sg_list = &recv_sgl[i];
      recv_wr[i].num_sge = 1;

      // Circular link
      recv_wr[i].next = (i < kRQDepth - 1) ? &recv_wr[i + 1] : &recv_wr[0];
    }

    // Fill the RECV queue. post_recvs() can use fast RECV and therefore not
    // actually fill the RQ, so post_recvs() isn't usable here.
    struct ibv_recv_wr *bad_wr;
    recv_wr[kRQDepth - 1].next = nullptr;  // Breaker of chains

    int ret = ibv_post_recv(qp, &recv_wr[0], &bad_wr);
    rt_assert(ret == 0, "Failed to fill RECV queue.");

    recv_wr[kRQDepth - 1].next = &recv_wr[0];  // Restore circularity
  }
}

void RawTransport::init_sends() {
  for (size_t i = 0; i < kPostlist; i++) {
    send_wr[i].next = &send_wr[i + 1];
    send_wr[i].opcode = IBV_WR_SEND;
    send_wr[i].sg_list = &send_sgl[i][0];
  }
}

}  // End erpc
