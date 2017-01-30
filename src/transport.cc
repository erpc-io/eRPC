#include "transport.h"

namespace ERpc {

Transport::Transport(TransportType transport_type, uint8_t phy_port,
                     HugeAllocator *huge_alloc, uint8_t app_tid)
    : transport_type(transport_type),
      phy_port(phy_port),
      huge_alloc(huge_alloc),
      app_tid(app_tid),
      numa_node(huge_alloc->get_numa_node()) {};

Transport::~Transport() {
  erpc_dprintf("eRPC Transport: Destroying transport for TID %u\n", app_tid);
}

}  // End ERpc
