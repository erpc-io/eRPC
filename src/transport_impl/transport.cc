#include "transport.h"
#include "util/huge_alloc.h"

namespace erpc {

Transport::Transport(TransportType transport_type, uint8_t rpc_id,
                     uint8_t phy_port, size_t numa_node, FILE *trace_file)
    : transport_type(transport_type),
      rpc_id(rpc_id),
      phy_port(phy_port),
      numa_node(numa_node),
      trace_file(trace_file) {}

Transport::~Transport() {}

}  // End erpc
