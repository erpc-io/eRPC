#include "transport.h"

namespace ERpc {

Transport::Transport(TransportType transport_type, size_t mtu, uint8_t app_tid,
                     uint8_t phy_port)
    : transport_type(transport_type),
      mtu(mtu),
      app_tid(app_tid),
      phy_port(phy_port){};

Transport::~Transport() {
  erpc_dprintf("eRPC Transport: Destroying transport for TID %u\n", app_tid);
}

}  // End ERpc
