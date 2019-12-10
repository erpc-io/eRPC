/**
 * @file verbs_common.h
 * @brief Common definitions for ibverbs-based transports
 */
#pragma once

#ifndef ERPC_DPDK

#include <dirent.h>
#include <infiniband/verbs.h>
#include <string>
#include "transport.h"
#include "util/logger.h"

namespace erpc {

// Constants for fast RECV driver mod
static constexpr uint64_t kMagicWrIDForFastRecv = 3185;
static constexpr uint64_t kModdedProbeWrID = 3186;
static constexpr int kModdedProbeRet = 3187;

/// Common information for verbs-based transports, resolved from a port ID
class VerbsResolve {
 public:
  int device_id = -1;  ///< Device index in list of verbs devices
  struct ibv_context *ib_ctx = nullptr;  ///< The verbs device context
  uint8_t dev_port_id = 0;  ///< 1-based port ID in device. 0 is invalid.
  size_t bandwidth = 0;     ///< Link bandwidth in bytes per second
};

static size_t enum_to_mtu(enum ibv_mtu mtu) {
  switch (mtu) {
    case IBV_MTU_256: return 256;
    case IBV_MTU_512: return 512;
    case IBV_MTU_1024: return 1024;
    case IBV_MTU_2048: return 2048;
    case IBV_MTU_4096: return 4096;
    default: return 0;
  }
}

static std::string link_layer_str(uint8_t link_layer) {
  switch (link_layer) {
    case IBV_LINK_LAYER_UNSPECIFIED: return "[Unspecified]";
    case IBV_LINK_LAYER_INFINIBAND: return "[InfiniBand]";
    case IBV_LINK_LAYER_ETHERNET: return "[Ethernet]";
    default: return "[Invalid]";
  }
}

/**
 * @brief A function wrapper whose \p pd argument is later bound to generate
 * this transport's \p reg_mr_func
 *
 * @throw runtime_error if memory registration fails
 */
static Transport::MemRegInfo ibv_reg_mr_wrapper(struct ibv_pd *pd, void *buf,
                                                size_t size) {
  struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
  rt_assert(mr != nullptr, "Failed to register mr.");

  ERPC_INFO("Registered %zu MB (lkey = %u)\n", size / MB(1), mr->lkey);
  return Transport::MemRegInfo(mr, mr->lkey);
}

/// A function wrapper used to generate a verbs transport's memory
/// deregistration function
static void ibv_dereg_mr_wrapper(Transport::MemRegInfo mr) {
  auto *ib_mr = reinterpret_cast<struct ibv_mr *>(mr.transport_mr);
  size_t size = ib_mr->length;
  uint32_t lkey = ib_mr->lkey;

  int ret = ibv_dereg_mr(ib_mr);
  if (ret != 0) {
    ERPC_ERROR("Memory degistration failed. size %zu B, lkey %u\n",
               size / MB(1), lkey);
  }

  ERPC_INFO("Deregistered %zu MB (lkey = %u)\n", size / MB(1), lkey);
}

/// Polls a CQ for one completion. In verbose mode only, prints a warning
/// message if polling gets stuck.
static inline void poll_cq_one_helper(struct ibv_cq *cq) {
  struct ibv_wc wc;
  size_t num_tries = 0;
  while (ibv_poll_cq(cq, 1, &wc) == 0) {
    // Do nothing while we have no CQE or poll_cq error
    if (ERPC_LOG_LEVEL == ERPC_LOG_LEVEL_INFO) {
      num_tries++;
      if (unlikely(num_tries == GB(1))) {
        fprintf(stderr, "eRPC: Warning. Stuck in poll_cq().");
        num_tries = 0;
      }
    }
  }

  if (unlikely(wc.status != 0)) {
    fprintf(stderr, "eRPC: Fatal error. Bad wc status %d.\n", wc.status);
    assert(false);
    exit(-1);
  }
}

/// Return the net interface for a verbs device (e.g., mlx5_0 -> enp4s0f0)
static std::string ibdev2netdev(std::string ibdev_name) {
  std::string dev_dir = "/sys/class/infiniband/" + ibdev_name + "/device/net";

  std::vector<std::string> net_ifaces;
  DIR *dp;
  struct dirent *dirp;
  dp = opendir(dev_dir.c_str());
  rt_assert(dp != nullptr, "Failed to open directory " + dev_dir);

  while (true) {
    dirp = readdir(dp);
    if (dirp == nullptr) break;

    if (strcmp(dirp->d_name, ".") == 0) continue;
    if (strcmp(dirp->d_name, "..") == 0) continue;
    net_ifaces.push_back(std::string(dirp->d_name));
  }
  closedir(dp);

  rt_assert(net_ifaces.size() > 0, "Directory " + dev_dir + " is empty");
  return net_ifaces[0];
}

static void common_resolve_phy_port(uint8_t phy_port, size_t mtu,
                                    TransportType transport_type,
                                    VerbsResolve &resolve) {
  std::ostringstream xmsg;  // The exception message
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
        const auto expected_link_layer =
            (transport_type == TransportType::kInfiniBand && !kIsRoCE)
                ? IBV_LINK_LAYER_INFINIBAND
                : IBV_LINK_LAYER_ETHERNET;
        if (port_attr.link_layer != expected_link_layer) {
          throw std::runtime_error("Invalid link layer. Port link layer is " +
                                   link_layer_str(port_attr.link_layer));
        }

        // Check the MTU
        size_t active_mtu = enum_to_mtu(port_attr.active_mtu);
        if (mtu > active_mtu) {
          throw std::runtime_error("Transport's required MTU is " +
                                   std::to_string(mtu) + ", active_mtu is " +
                                   std::to_string(active_mtu));
        }

        resolve.device_id = dev_i;
        resolve.ib_ctx = ib_ctx;
        resolve.dev_port_id = port_i;

        // Compute the bandwidth
        double gbps_per_lane = -1;
        switch (port_attr.active_speed) {
          case 1: gbps_per_lane = 2.5; break;
          case 2: gbps_per_lane = 5.0; break;
          case 4: gbps_per_lane = 10.0; break;
          case 8: gbps_per_lane = 10.0; break;
          case 16: gbps_per_lane = 14.0; break;
          case 32: gbps_per_lane = 25.0; break;
          default: rt_assert(false, "Invalid active speed");
        };

        size_t num_lanes = SIZE_MAX;
        switch (port_attr.active_width) {
          case 1: num_lanes = 1; break;
          case 2: num_lanes = 4; break;
          case 4: num_lanes = 8; break;
          case 8: num_lanes = 12; break;
          default: rt_assert(false, "Invalid active width");
        };

        double total_gbps = num_lanes * gbps_per_lane;
        resolve.bandwidth = total_gbps * (1000 * 1000 * 1000) / 8.0;

        ERPC_INFO(
            "Port %u resolved to device %s, port %u. Speed = %.2f Gbps.\n",
            phy_port, ib_ctx->device->name, port_i, total_gbps);

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
  xmsg << "Failed to resolve verbs port index " << std::to_string(phy_port);
  throw std::runtime_error(xmsg.str());
}
}  // namespace erpc

#endif
