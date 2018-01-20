#include <stdio.h>
#include "rpc.h"

static const std::string kServerHostname =
    "akaliaNode-1.RDMA.fawn.apt.emulab.net";
static const std::string kClientHostname =
    "akaliaNode-2.RDMA.fawn.apt.emulab.net";

static constexpr uint16_t kUDPPort = 31850;
static constexpr uint8_t kReqType = 2;
static constexpr size_t kMsgSize = 16;
