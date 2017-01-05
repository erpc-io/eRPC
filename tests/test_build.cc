#include "rpc.h"

int main() {
  ERpc::Nexus nexus;
  ERpc::Rpc<ERpc::InfiniBandTransport> rpc(nexus);
}
