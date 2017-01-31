#include "transport.h"

namespace ERpc {

Transport::Transport(TransportType transport_type, size_t mtu, uint8_t app_tid,
                     uint8_t phy_port, HugeAllocator *huge_alloc)
    : transport_type(transport_type),
      mtu(mtu),
      app_tid(app_tid),
      phy_port(phy_port),
      huge_alloc(huge_alloc),
      numa_node(huge_alloc->get_numa_node()){};

Transport::~Transport() {
  erpc_dprintf("eRPC Transport: Destroying transport for TID %u\n", app_tid);
}

}  // End ERpc
