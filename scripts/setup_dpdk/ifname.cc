#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Print the name of an interface whose IP address starts with 10.
int main() {
  struct ifaddrs *addrs;
  getifaddrs(&addrs);

  for (struct ifaddrs *iap = addrs; iap != NULL; iap = iap->ifa_next) {
    // Consider only active IPv4 iterfaces
    if (iap->ifa_addr && (iap->ifa_flags & IFF_UP) &&
        iap->ifa_addr->sa_family == AF_INET) {
      auto *sa = reinterpret_cast<struct sockaddr_in *>(iap->ifa_addr);

      char ip_addr[32];
      inet_ntop(iap->ifa_addr->sa_family, &(sa->sin_addr), ip_addr,
                sizeof(ip_addr));

      // Check for 10.
      if (ip_addr[0] == '1' && ip_addr[1] == '0' && ip_addr[2] == '.') {
        printf("%s\n", iap->ifa_name);
        freeifaddrs(addrs);
        return 0;
      }
    }
  }

  freeifaddrs(addrs);
  return 0;
}
