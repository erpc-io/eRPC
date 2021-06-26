#ifdef ERPC_FAKE
#include <cstdlib>
#include "fake_transport.h"

namespace erpc {

constexpr size_t FakeTransport::kMaxDataPerPkt;

}  // namespace erpc

#endif
