#include "transport.h"

namespace ERpc {

Transport::Transport(TransportType transport_type, uint8_t app_tid,
                     uint8_t phy_port)
    : transport_type(transport_type), app_tid(app_tid), phy_port(phy_port){};

Transport::~Transport() {}

}  // End ERpc
