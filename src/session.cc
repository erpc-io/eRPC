#include "session.h"

namespace ERpc {

Session::Session() : is_cc(false) {}

Session::~Session(){};

std::string Session::get_client_name() {
  std::string ret;

  ret += std::string("[");
  ret += std::string(client.hostname);
  ret += std::string(", ");
  ret += std::to_string(client.app_tid);
  ret += std::string("]");

  return ret;
}

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc
