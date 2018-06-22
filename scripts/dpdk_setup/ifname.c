#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Print the interface name whose IP address contains argv[1] as a prefix
int main(int argc, char *argv[]) {
  struct ifaddrs *addrs, *iap;
  struct sockaddr_in *sa;
  char buf[32];

  getifaddrs(&addrs);
  for (iap = addrs; iap != NULL; iap = iap->ifa_next) {
    if (iap->ifa_addr && (iap->ifa_flags & IFF_UP) &&
        iap->ifa_addr->sa_family == AF_INET) {
      sa = (struct sockaddr_in *)(iap->ifa_addr);
      inet_ntop(iap->ifa_addr->sa_family, (void *)&(sa->sin_addr), buf,
                sizeof(buf));
      if (strstr(buf, argv[1]) != NULL) {
        printf("%s\n", iap->ifa_name);
        freeifaddrs(addrs);
        return 0;
      }
    }
  }
  freeifaddrs(addrs);
  return 0;
}
