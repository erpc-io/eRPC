#ifdef ERPC_FAKE
#include "fake_transport.h"
#include <cstdlib>

namespace erpc {

constexpr size_t FakeTransport::kMaxDataPerPkt;

}  // namespace erpc

#endif
