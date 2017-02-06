#include "ib_transport.h"

namespace ERpc {
void IBTransport::send_message(Session *session, const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

void IBTransport::poll_completions() {}

}  // End ERpc
