#include "transport.h"

namespace ERpc {

Transport::Transport(TransportType transport_type, uint8_t rpc_id,
                     uint8_t phy_port)
    : transport_type(transport_type), rpc_id(rpc_id), phy_port(phy_port){};

Transport::~Transport() {}

}  // End ERpc
