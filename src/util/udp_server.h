#pragma once

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#define ASIO_STANDLONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

namespace erpc {

/// Basic UDP server class that supports receiving messages
template <class T>
class UDPServer {
 public:
  UDPServer(uint16_t port, size_t timeout_ms) : timeout_ms_(timeout_ms) {
    socket_ = new asio::ip::udp::socket(
        io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port));
  }

  UDPServer() {}
  UDPServer(const UDPServer &) = delete;

  ~UDPServer() {
    if (socket_ != nullptr) delete socket_;
  }

  size_t recv_blocking(T &msg) {
    size_t ret = socket_->receive(
        asio::buffer(reinterpret_cast<void *>(&msg), sizeof(T)));
    return ret;
  }

 private:
  asio::io_context io_context_;
  asio::ip::udp::socket *socket_;
  size_t timeout_ms_;
};

}  // namespace erpc
