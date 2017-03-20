#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <algorithm>

#include "common.h"
#include "nexus.h"
#include "rpc.h"
#include "util/barrier.h"

namespace ERpc {

Nexus::Nexus(uint16_t mgmt_udp_port) : Nexus(mgmt_udp_port, 0.0) {}

Nexus::Nexus(uint16_t mgmt_udp_port, double udp_drop_prob)
    : udp_config(mgmt_udp_port, udp_drop_prob),
      freq_ghz(get_freq_ghz()),
      hostname(get_hostname()) {
  /* Print configuration messages if verbose printing or checking is enabled */
  if (kDatapathVerbose) {
    fprintf(stderr,
            "eRPC Nexus: Datapath verbose enabled. Performance will be low.\n");
  }

  if (kDatapathChecks) {
    fprintf(stderr,
            "eRPC Nexus: Datapath checks enabled. Performance will be low.\n");
  }

  nexus_object = this;

  install_sigio_handler();

  erpc_dprintf("eRPC Nexus: Created with global UDP port %u, hostname %s.\n",
               mgmt_udp_port, hostname.c_str());
}

Nexus::~Nexus() {
  erpc_dprintf_noargs("eRPC Nexus: Destroying Nexus.\n");
  close(sm_sock_fd);
}

bool Nexus::app_tid_exists(uint8_t app_tid) {
  nexus_lock.lock();
  bool found = false;

  for (nexus_hook_t *reg_hook : reg_hooks) {
    if (reg_hook->app_tid == app_tid) {
      found = true;
    }
  }

  nexus_lock.unlock();
  return found;
}

void Nexus::register_hook(nexus_hook_t *hook) {
  assert(hook != nullptr);
  assert(!app_tid_exists(hook->app_tid));

  nexus_lock.lock();
  reg_hooks.push_back(hook);
  nexus_lock.unlock();
}

void Nexus::unregister_hook(nexus_hook_t *hook) {
  assert(hook != nullptr);
  assert(app_tid_exists(hook->app_tid));

  nexus_lock.lock();

  size_t initial_size = reg_hooks.size(); /* Debug-only */
  _unused(initial_size);
  reg_hooks.erase(std::remove(reg_hooks.begin(), reg_hooks.end(), hook),
                  reg_hooks.end());
  assert(reg_hooks.size() == initial_size - 1);

  nexus_lock.unlock();
}

void Nexus::install_sigio_handler() {
  /*
   * Create a UDP socket.
   * AF_INET = IPv4, SOCK_DGRAM = datagrams, IPPROTO_UDP = datagrams over UDP.
   * Returns a file descriptor.
   */
  sm_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sm_sock_fd < 0) {
    erpc_dprintf_noargs("eRPC Nexus: FATAL. Error opening datagram socket.\n");
    throw std::runtime_error("eRPC Nexus: Error opening datagram socket.");
  }

  /*
   * Bind the socket to accept packets destined to any IP interface of this
   * machine (INADDR_ANY), and to port @mgmt_udp_port.
   */
  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons((uint16_t)udp_config.mgmt_udp_port);

  if (bind(sm_sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) <
      0) {
    erpc_dprintf_noargs("eRPC Nexus: FATAL. Error binding datagram socket.\n");
    throw std::runtime_error("eRPC Nexus: Error binding datagram socket.");
  }

  /* Set file flags. Allow receipt of asynchronous I/O signals */
  if (fcntl(sm_sock_fd, F_SETFL, O_ASYNC | O_NONBLOCK) < 0) {
    erpc_dprintf_noargs("eRPC Nexus: FATAL. Ascync fnctl() failed.\n");
    throw std::runtime_error("eRPC Nexus: Async fnctl() failed.");
  }

  /* Ensure that only the thread that creates the Nexus receives SIGIO */
  struct f_owner_ex owner_thread;
  owner_thread.type = F_OWNER_TID;
  owner_thread.pid = (int)syscall(SYS_gettid);

  if (fcntl(sm_sock_fd, F_SETOWN_EX, &owner_thread) < 0) {
    erpc_dprintf_noargs("eRPC Nexus: FATAL. Setown fnctl() failed.\n");
    throw std::runtime_error("eRPC Nexus: Setown fnctl() failed.");
  }

  /*
   * Set up a SIGIO signal handler **after** fixing the thread that will
   * receive this signal. The sigaction man page specifies that calling
   * sigaction without SA_NODEFER set ensures that the signal handler won't
   * be interrupted by the same signal while it is running. It may be
   * interrupted by other signals, however.
   */
  struct sigaction act;
  memset((void *)&act, 0, sizeof(act));
  act.sa_handler = &sigio_handler;
  if (sigaction(SIGIO, &act, nullptr) < 0) { /* Old signal handler is NULL */
    erpc_dprintf_noargs("eRPC Nexus: FATAL. sigaction() failed.\n");
    throw std::runtime_error("eRPC Nexus: sigaction() failed.");
  }
}

void Nexus::session_mgnt_handler() {
  nexus_lock.lock();

  uint32_t addr_len = sizeof(struct sockaddr_in); /* value-result */
  struct sockaddr_in their_addr; /* Sender's address information goes here */

  SessionMgmtPkt *sm_pkt = new SessionMgmtPkt(); /* Need new: passed to Rpc */
  int flags = 0;

  /* Receive a packet from the socket. We're guaranteed to get exactly one. */
  ssize_t recv_bytes =
      recvfrom(sm_sock_fd, (void *)sm_pkt, sizeof(*sm_pkt), flags,
               (struct sockaddr *)&their_addr, &addr_len);
  if (recv_bytes != (ssize_t)sizeof(*sm_pkt)) {
    erpc_dprintf(
        "eRPC Nexus: FATAL. Received unexpected data size (%zd) from "
        "session management socket. Expected = %zu.\n",
        recv_bytes, sizeof(*sm_pkt));
    assert(false);
    exit(-1);
  }

  if (!session_mgmt_pkt_type_is_valid(sm_pkt->pkt_type)) {
    erpc_dprintf(
        "eRPC Nexus: FATAL. Received session management packet of "
        "unexpected type %d.\n",
        static_cast<int>(sm_pkt->pkt_type));
    assert(false);
    exit(-1);
  }

  uint8_t target_app_tid; /* TID of the Rpc that should handle this packet */
  const char *source_hostname;
  uint8_t source_app_tid; /* Debug-only */
  _unused(source_app_tid);

  bool is_sm_req = session_mgmt_pkt_type_is_req(sm_pkt->pkt_type);

  if (is_sm_req) {
    target_app_tid = sm_pkt->server.app_tid;
    source_app_tid = sm_pkt->client.app_tid;
    source_hostname = sm_pkt->client.hostname;
  } else {
    target_app_tid = sm_pkt->client.app_tid;
    source_app_tid = sm_pkt->server.app_tid;
    source_hostname = sm_pkt->server.hostname;
  }

  /* Find the registered Rpc that has this TID */
  nexus_hook_t *target_hook = nullptr;
  for (nexus_hook_t *hook : reg_hooks) {
    if (hook->app_tid == target_app_tid) {
      target_hook = hook;
    }
  }

  if (target_hook == nullptr) {
    /* We don't have an Rpc object for @target_app_tid  */

    if (is_sm_req) {
      /* If it's a request, we must send a response */
      erpc_dprintf(
          "eRPC Nexus: Received session management request for invalid Rpc %u "
          "from Rpc [%s, %u]. Sending response.\n",
          target_app_tid, source_hostname, source_app_tid);

      sm_pkt->send_resp_mut(SessionMgmtErrType::kInvalidRemoteAppTid,
                            &udp_config);
    } else {
      /* If it's a response, we can ignore it */
      erpc_dprintf(
          "eRPC Nexus: Received session management resp for invalid Rpc %u "
          "from Rpc [%s, %u]. Ignoring.\n",
          target_app_tid, source_hostname, source_app_tid);
    }

    delete sm_pkt;
    return;
  }

  /* Add the packet to the target Rpc's session management packet list */
  target_hook->lock();

  target_hook->session_mgmt_pkt_list.push_back(sm_pkt);
  ERpc::memory_barrier();
  target_hook->session_mgmt_pkt_counter++;

  target_hook->unlock();
  nexus_lock.unlock();
}

double Nexus::get_freq_ghz() {
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);
  uint64_t rdtsc_start = rdtsc();

  /*
   * Do not change this loop! The hardcoded value below depends on this loop
   * and prevents it from being optimized out.
   */
  uint64_t sum = 5;
  for (uint64_t i = 0; i < 1000000; i++) {
    sum += i + (sum + i) * (i % sum);
  }

  if (sum != 13580802877818827968ull) {
    erpc_dprintf_noargs("eRPC: FATAL. Failed in rdtsc frequency measurement.");
    assert(false);
    exit(-1);
  }

  clock_gettime(CLOCK_REALTIME, &end);
  uint64_t clock_ns = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000 +
                      (uint64_t)(end.tv_nsec - start.tv_nsec);
  uint64_t rdtsc_cycles = rdtsc() - rdtsc_start;

  double _freq_ghz = rdtsc_cycles / clock_ns;

  /* Less than 500 MHz and greater than 5.0 GHz is abnormal */
  if (_freq_ghz < 0.5 || _freq_ghz > 5.0) {
    erpc_dprintf("eRPC Nexus: FATAL. Abnormal CPU frequency %.4f GHz\n",
                 _freq_ghz);
    throw std::runtime_error("eRPC Nexus: get_freq_ghz() failed.");
  }

  return _freq_ghz;
}

std::string Nexus::get_hostname() {
  char _hostname[kMaxHostnameLen];

  /* Get the local hostname */
  int ret = get_hostname(_hostname);
  if (ret == -1) {
    erpc_dprintf("eRPC Nexus: FATAL. gethostname failed. Error = %s.\n",
                 strerror(errno));
    throw std::runtime_error("eRPC Nexus: get_hostname() failed.");
  }

  return std::string(_hostname);
}

}  // End ERpc
