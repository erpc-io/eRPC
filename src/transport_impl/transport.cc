#include "transport.h"

namespace erpc {

Transport::Transport(TransportType transport_type, uint8_t epid, uint8_t rpc_id,
                     uint8_t phy_port, size_t numa_node)
    : transport_type(transport_type),
      epid(epid),
      rpc_id(rpc_id),
      phy_port(phy_port),
      numa_node(numa_node) {}

Transport::~Transport() {}

}  // End erpc
