#pragma once

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#define ASIO_STANDLONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <memory>

namespace erpc {

/// Basic UDP client class that supports sending messages and caches remote
/// addrinfo mappings
template <class T>
class UDPClient {
 public:
  UDPClient()
      : socket_(new asio::ip::udp::socket(io_context_)),
        resolver_(new asio::ip::udp::resolver(io_context_)) {
    socket_->open(asio::ip::udp::v4());
  }

  UDPClient(const UDPClient &) = delete;

  ~UDPClient() {}

  size_t send(const std::string rem_hostname, uint16_t rem_port,
               const T &msg) {
    asio::error_code error;
    asio::ip::udp::resolver::results_type results =
        resolver_->resolve(rem_hostname, std::to_string(rem_port), error);

    if (results.size() == 0) {
      char issue_msg[1000];
      sprintf(issue_msg, "Failed to resolve %s, asio error = %s.",
              rem_hostname.c_str(), error.message().c_str());
      throw std::runtime_error(issue_msg);
    }

    asio::ip::udp::endpoint endpoint = *results.begin();
    const size_t ret =
        socket_->send_to(asio::buffer(&msg, sizeof(T)), endpoint);
    return ret;
  }

  /// Maintain a all packets sent by this client
  void enable_recording() { enable_recording_flag_ = true; }

 private:
  asio::io_context io_context_;
  std::unique_ptr<asio::ip::udp::socket> socket_;
  std::unique_ptr<asio::ip::udp::resolver> resolver_;

  /// The list of all packets sent, maintained if recording is enabled
  std::vector<T> sent_vec_;
  bool enable_recording_flag_ = false;  /// Flag to enable recording for testing
};                                      // namespace erpc

}  // namespace erpc
