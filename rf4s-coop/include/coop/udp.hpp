#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace coop {

struct Endpoint {
  uint32_t ipv4 = 0;  // host byte order
  uint16_t port = 0;

  bool operator==(const Endpoint&) const = default;
  std::string toString() const;
};

struct Datagram {
  Endpoint from;
  std::vector<uint8_t> data;
};

std::optional<Endpoint> resolveIPv4(const std::string& host, uint16_t port);

// Non-blocking IPv4 UDP socket for LAN play and development. Internet play
// will move to Steam Networking Sockets (relays, NAT traversal, reliability).
class UdpSocket {
 public:
  // port 0 picks a free port.
  static std::optional<UdpSocket> open(uint16_t port);

  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  ~UdpSocket();

  bool sendTo(const Endpoint& to, const std::vector<uint8_t>& data);
  // Returns nullopt when nothing is waiting.
  std::optional<Datagram> receive();
  uint16_t localPort() const;

 private:
  explicit UdpSocket(intptr_t handle) : handle_(handle) {}
  void close();

  intptr_t handle_ = -1;
};

}  // namespace coop
