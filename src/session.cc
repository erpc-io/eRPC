#include "session.h"

namespace ERpc {

Session::Session(const char *_rem_hostname, TransportType transport_type,
                 uint16_t rem_dev_port_index)
    : transport_type(transport_type), rem_dev_port_index(rem_dev_port_index) {
  if (transport_type == TransportType::Invalid) {
    fprintf(stderr, "ERpc: Invalid transport type\n");
  }

  rem_hostname = std::string(_rem_hostname);
}

Session::~Session(){};

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc
