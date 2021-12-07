#pragma once

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#define ASIO_STANDLONE
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <memory>
#include "logger.h"

namespace erpc {

/// Basic UDP client class that supports sending messages and caches remote
/// addrinfo mappings
template <class T>
class UDPClient {
 public:
  UDPClient()
      : resolver_(new asio::ip::udp::resolver(io_context_)),
        socket_(new asio::ip::udp::socket(io_context_)) {
    socket_->open(asio::ip::udp::v4());
  }

  UDPClient(const UDPClient &) = delete;

  ~UDPClient() {}

  /**
   * @brief Send a UDP message to a remote host
   *
   * @param rem_hostname DNS-resolvable name of the remote host
   * @param rem_port Destination UDP port to send the message to
   * @param msg Contents of the message
   *
   * @return Number of bytes sent on success, SIZE_MAX on failure
   */
  size_t send(const std::string rem_hostname, uint16_t rem_port, const T &msg) {
    asio::error_code error;
    asio::ip::udp::resolver::results_type results =
        resolver_->resolve(rem_hostname, std::to_string(rem_port), error);

    if (results.size() == 0) {
      ERPC_ERROR("eRPC: Failed to resolve %s, asio error = %s.\n",
                 rem_hostname.c_str(), error.message().c_str());
      return SIZE_MAX;
    }

    // Pick an IPv4 endpoint
    for (const auto &endpoint_iter : results) {
      if (!endpoint_iter.endpoint().address().is_v4()) continue;

      try {
        const size_t ret =
            socket_->send_to(asio::buffer(&msg, sizeof(T)), endpoint_iter);
        if (enable_recording_flag_) sent_vec_.push_back(msg);
        return ret;
      } catch (const asio::system_error &e) {
        ERPC_ERROR("eRPC: asio send_to() failed to %s, error: %s\n",
                   rem_hostname.c_str(), e.what());
        return SIZE_MAX;
      }
    }

    // We failed to find an IPv4 endpoint
    ERPC_ERROR(
        "eRPC: Failed to find an IPv4 endpoint to %s. Found %zu non-IPv4 "
        "endpoints to %s though.\n",
        rem_hostname.c_str(), results.size(), rem_hostname.c_str());
    return SIZE_MAX;
  }

  /// Maintain a all packets sent by this client
  void enable_recording() { enable_recording_flag_ = true; }

 private:
  asio::io_context io_context_;
  std::unique_ptr<asio::ip::udp::resolver> resolver_;
  std::unique_ptr<asio::ip::udp::socket> socket_;

  /// The list of all packets sent, maintained if recording is enabled
  std::vector<T> sent_vec_;
  bool enable_recording_flag_ = false;  /// Flag to enable recording for testing
};                                      // namespace erpc

}  // namespace erpc
