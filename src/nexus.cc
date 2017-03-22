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

Nexus::Nexus(uint16_t mgmt_udp_port, size_t num_bg_threads,
             double udp_drop_prob)
    : udp_config(mgmt_udp_port, udp_drop_prob),
      freq_ghz(get_freq_ghz()),
      hostname(get_hostname()),
      num_bg_threads(num_bg_threads) {
  // Print warning messages if low-performance debug settings are enabled
  if (kDatapathVerbose) {
    fprintf(stderr,
            "eRPC Nexus: Datapath verbose enabled. Performance will be low.\n");
  }

  if (kDatapathChecks) {
    fprintf(stderr,
            "eRPC Nexus: Datapath checks enabled. Performance will be low.\n");
  }

  if (num_bg_threads > kMaxBgThreads) {
    throw std::runtime_error("eRPC Nexus: Too many background threads.");
  }

  if (udp_drop_prob > kMaxUdpDropProb) {
    throw std::runtime_error("eRPC Nexus: UDP drop probability too high.");
  }

  nexus_object = this;

  // Launch background threads
  bg_kill_switch = false;
  for (size_t i = 0; i < num_bg_threads; i++) {
    erpc_dprintf("eRPC Nexus: Launching background thread %zu.\n", i);

    bg_thread_ctx_arr[i].ops_arr = &ops_arr;
    bg_thread_ctx_arr[i].bg_kill_switch = &bg_kill_switch;
    bg_thread_ctx_arr[i].bg_thread_id = i;

    bg_thread_arr[i] = std::thread(bg_thread_func, &bg_thread_ctx_arr[i]);
  }

  install_sigio_handler();

  erpc_dprintf("eRPC Nexus: Created with global UDP port %u, hostname %s.\n",
               mgmt_udp_port, hostname.c_str());
}

Nexus::~Nexus() {
  erpc_dprintf_noargs("eRPC Nexus: Destroying Nexus.\n");

  /* Close the socket file descriptor */
  int ret = close(sm_sock_fd);
  if (ret != 0) {
    erpc_dprintf_noargs(
        "eRPC Nexus: Failed to close session management socket. Ignoring.\n");
  }

  bg_kill_switch = true; /* Indicate background threads to kill themselves */
  for (size_t i = 0; i < num_bg_threads; i++) {
    bg_thread_arr[i].join();
  }
}

bool Nexus::app_tid_exists(uint8_t app_tid) {
  nexus_lock.lock();
  bool ret = (reg_hooks_arr[app_tid] != nullptr);
  nexus_lock.unlock();
  return ret;
}

void Nexus::register_hook(NexusHook *hook) {
  assert(hook != nullptr);

  uint8_t app_tid = hook->app_tid;
  assert(app_tid <= kMaxAppTid);
  assert(reg_hooks_arr[app_tid] == nullptr);

  nexus_lock.lock();

  ops_registration_allowed = false; /* Disable future Ops registration */
  reg_hooks_arr[app_tid] = hook;

  /* Install background request and response submission lists */
  for (size_t i = 0; i < num_bg_threads; i++) {
    BgThreadCtx &bg_ctx = bg_thread_ctx_arr[i];
    assert(bg_ctx.bg_thread_id == i);

    hook->bg_req_list_arr[i] = &bg_ctx.bg_req_list; /* Request */
    bg_ctx.bg_resp_list_arr[app_tid] = &hook->bg_resp_list;
  }

  nexus_lock.unlock();
}

void Nexus::unregister_hook(NexusHook *hook) {
  assert(hook != nullptr);

  uint8_t app_tid = hook->app_tid;
  assert(app_tid <= kMaxAppTid);
  assert(reg_hooks_arr[app_tid] == hook);

  nexus_lock.lock();
  reg_hooks_arr[app_tid] = nullptr;
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
  NexusHook *target_hook = reg_hooks_arr[target_app_tid];

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
  target_hook->sm_pkt_list.unlocked_push_back(sm_pkt);
  nexus_lock.unlock();
}

int Nexus::register_ops(uint8_t req_type, Ops app_ops) {
  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Nexus: Failed to register handlers for request type %u. Issue",
          req_type);

  /* If any Rpc is already registered, the user cannot register new Ops */
  if (!ops_registration_allowed) {
    erpc_dprintf("%s: Registration not allowed anymore.\n", issue_msg);
    return EPERM;
  }

  Ops &arr_ops = ops_arr[req_type];

  /* Check if this request type is already registered */
  if (ops_arr[req_type].is_valid()) {
    erpc_dprintf("%s: A handler for this request type already exists.\n",
                 issue_msg);
    return EEXIST;
  }

  /* Check if the application's Ops is valid */
  if (!app_ops.is_valid()) {
    erpc_dprintf("%s: Invalid handler.\n", issue_msg);
    return EINVAL;
  }

  /* If the request handler runs in the background, we must have bg threads */
  if (app_ops.run_in_background && num_bg_threads == 0) {
    erpc_dprintf("%s: Background threads not available.\n", issue_msg);
    return EPERM;
  }

  arr_ops = app_ops;
  return 0;
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
