#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/transport.hpp"

namespace coop {

std::optional<Endpoint> resolveIPv4(const std::string& host, uint16_t port);

// Non-blocking IPv4 UDP socket for LAN play and development. Internet play
// will move to Steam Networking Sockets (relays, NAT traversal, reliability).
class UdpSocket : public ITransport {
 public:
  // port 0 picks a free port.
  static std::optional<UdpSocket> open(uint16_t port);

  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  ~UdpSocket() override;

  bool sendTo(const Endpoint& to, const std::vector<uint8_t>& data) override;
  std::optional<Datagram> receive() override;
  uint16_t localPort() const override;

 private:
  explicit UdpSocket(intptr_t handle) : handle_(handle) {}
  void close();

  intptr_t handle_ = -1;
};

}  // namespace coop
