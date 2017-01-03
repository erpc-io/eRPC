#include "rpc.h"
#include "nexus.h"

namespace ERpc {

Rpc::Rpc(Nexus &nexus, Transport &transport)
    : nexus(nexus), transport(transport){};

Rpc::~Rpc() {}

void Rpc::send_request(const Session &session, const Buffer &buffer) {
  _unused(session);
  _unused(buffer);
}

void Rpc::send_response(const Session &session, const Buffer &buffer) {
  _unused(session);
  _unused(buffer);
};

void Rpc::run_event_loop() {};

}
