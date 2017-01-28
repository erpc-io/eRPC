#include <stdexcept>
#include <sstream>

#include "ib_transport.h"
#include "util/udp_client.h"

namespace ERpc {


IBTransport::IBTransport(HugeAllocator *huge_alloc, uint8_t phy_port)
    : Transport(TransportType::kInfiniBand, phy_port, huge_alloc) {
  init_infiniband_structs();
}

IBTransport::~IBTransport() {
  /* Do not destroy @huge_alloc; the parent Rpc will do it. */
}

void IBTransport::fill_routing_info(RoutingInfo *routing_info) const {
  memset((void *)routing_info, 0, kMaxRoutingInfoSize);
  return;
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
      xmsg << "eRPC IBTransport: Failed to open InfiniBand device " << dev_i;
      throw std::runtime_error(xmsg.str());
    }

		struct ibv_device_attr device_attr;
		memset(&device_attr, 0, sizeof(device_attr));
		if (ibv_query_device(ib_ctx, &device_attr) != 0) {
      xmsg << "eRPC IBTransport: Failed to query InfiniBand device " << dev_i;
      throw std::runtime_error(xmsg.str());
		}

		for (uint8_t port_i = 1; port_i <= device_attr.phys_port_cnt; port_i ++) {
			/* Count this port only if it is enabled */
			struct ibv_port_attr port_attr;
			if (ibv_query_port(ib_ctx, port_i, &port_attr) != 0) {
        xmsg << "eRPC IBTransport: Failed to query port " << port_i <<
            "on device " << dev_i;
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
        return;
			}

			ports_to_discover--;
		}

    /* Thank you Mario, but our port is in another device */
		if (ibv_close_device(ib_ctx) != 0) {
      xmsg << "eRPC IBTransport: Failed to close InfiniBand device " << dev_i;
      throw std::runtime_error(xmsg.str());
		}
	}

	/* If we are here, port resolution has failed */
	assert(device_id == -1 && dev_port_id == -1);
  xmsg << "eRPC IBTransport: Failed to resolve InfiniBand port index " <<
      phy_port;
  throw std::runtime_error(xmsg.str());
}

void IBTransport::init_infiniband_structs() {
}

}
