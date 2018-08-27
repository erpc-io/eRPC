#include <stdio.h>
#include "rpc.h"

static const std::string kServerHostname = "130.127.133.179";
static const std::string kClientHostname = "130.127.133.145";

static constexpr uint16_t kUDPPort = 31850;
static constexpr uint8_t kReqType = 2;
static constexpr size_t kMsgSize = 16;
