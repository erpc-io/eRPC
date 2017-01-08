#include "session.h"

namespace ERpc {

Session::Session() : is_cc(false) {}

Session::~Session(){};

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc
