#include "session.h"

namespace ERpc {

Session::Session(int session_num, const char *_rem_hostname,
                 int rem_fdev_port_index)
    : session_num(session_num), rem_hostname(_rem_hostname),
      rem_fdev_port_index(rem_fdev_port_index) {}

Session::~Session(){};

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc
