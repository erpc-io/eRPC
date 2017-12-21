#include <stdio.h>
#include "rpc.h"
using namespace erpc;

#define UDP_PORT 31851
#define REQ_TYPE 1
#define SERVER_ID 2
#define CLIENT_ID 3

static constexpr size_t kMsgSize = 16;

#define HelloTransport RawTransport

static constexpr ReqFuncType kForeground = ReqFuncType::kForeground;
