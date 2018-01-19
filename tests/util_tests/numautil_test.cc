#include "util/numautils.h"
using namespace erpc;

int main() {
  std::vector<size_t> lcore_0_vec = get_lcores_for_numa_node(0);
  for (size_t l : lcore_0_vec) {
    printf("%zu\n", l);
  }
}
