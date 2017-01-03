#include "rpc.h"
#include "nexus.h"

int main() {
  ERpc::Nexus nexus;
  ERpc::Transport transport;

  ERpc::Rpc rpc(nexus, transport);
}
