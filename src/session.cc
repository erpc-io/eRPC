#include "session.h"

namespace ERpc {

Session::Session(const char *transport_name, const char *_hostname,
                 int rem_port_index)
    : rem_port_index(rem_port_index) {
  transport_type = get_transport_type(transport_name);
  if (transport_type == TransportType::Invalid) {
    fprintf(stderr, "ERpc: Invalid transport type %s\n", transport_name);
  }

  hostname = std::string(_hostname);
}

Session::~Session(){};

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }
}
