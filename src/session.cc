#include "session.h"

namespace ERpc {

Session::Session(const char *_rem_hostname, int rem_fdev_port_index,
                 uint16_t nexus_udp_port)
    : rem_hostname(_rem_hostname),
      rem_fdev_port_index(rem_fdev_port_index),
      nexus_udp_port(nexus_udp_port) {}

Session::~Session(){};

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc
