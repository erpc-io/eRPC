extern "C" 
{
  #include <raft/raft.h>
}

#include "rpc.h"

int main() {
  raft_server_t* raft = raft_new();
  _unused(raft);
}
