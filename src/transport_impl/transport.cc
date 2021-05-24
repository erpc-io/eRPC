#include "transport.h"
#include "util/huge_alloc.h"

namespace erpc {

Transport::Transport(TransportType transport_type, uint8_t rpc_id,
                     uint8_t phy_port, size_t numa_node, FILE *trace_file)
    : transport_type_(transport_type),
      rpc_id_(rpc_id),
      phy_port_(phy_port),
      numa_node_(numa_node),
      trace_file_(trace_file) {}

Transport::~Transport() {}

}  // namespace erpc
