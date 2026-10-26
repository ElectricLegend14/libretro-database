#include "coop/udp.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <cstring>

namespace coop {
namespace {

constexpr intptr_t kInvalid = -1;
constexpr size_t kMaxDatagram = 1500;

#ifdef _WIN32
bool initSockets() {
  static const bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}
SOCKET raw(intptr_t h) { return static_cast<SOCKET>(h); }
#else
bool initSockets() { return true; }
int raw(intptr_t h) { return static_cast<int>(h); }
#endif

sockaddr_in toSockaddr(const Endpoint& e) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(e.ipv4);
  addr.sin_port = htons(e.port);
  return addr;
}

}  // namespace

std::optional<Endpoint> resolveIPv4(const std::string& host, uint16_t port) {
  if (!initSockets()) return std::nullopt;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return std::nullopt;
  auto* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
  Endpoint e{ntohl(addr->sin_addr.s_addr), port};
  freeaddrinfo(result);
  return e;
}

std::optional<UdpSocket> UdpSocket::open(uint16_t port) {
  if (!initSockets()) return std::nullopt;
  auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  auto handle = static_cast<intptr_t>(s);
  if (handle == kInvalid) return std::nullopt;
  UdpSocket sock(handle);

  sockaddr_in addr = toSockaddr({INADDR_ANY, port});
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return std::nullopt;

#ifdef _WIN32
  u_long nonBlocking = 1;
  if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0) return std::nullopt;
#else
  int flags = fcntl(s, F_GETFL, 0);
  if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) return std::nullopt;
#endif
  return sock;
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalid; }

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = kInvalid;
  }
  return *this;
}

UdpSocket::~UdpSocket() { close(); }

void UdpSocket::close() {
  if (handle_ == kInvalid) return;
#ifdef _WIN32
  closesocket(raw(handle_));
#else
  ::close(raw(handle_));
#endif
  handle_ = kInvalid;
}

bool UdpSocket::sendTo(const Endpoint& to, const std::vector<uint8_t>& data) {
  sockaddr_in addr = toSockaddr(to);
  auto sent = sendto(raw(handle_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                     reinterpret_cast<sockaddr*>(&addr), sizeof addr);
  return sent == static_cast<decltype(sent)>(data.size());
}

std::optional<Datagram> UdpSocket::receive() {
  // Loop past errors such as Windows' WSAECONNRESET, which UDP sockets report
  // after an earlier send hit a closed port; they don't mean the queue is empty.
  for (int attempt = 0; attempt < 8; ++attempt) {
    Datagram d;
    d.data.resize(kMaxDatagram);
    sockaddr_in addr{};
    socklen_t addrLen = sizeof addr;
    auto n = recvfrom(raw(handle_), reinterpret_cast<char*>(d.data.data()), static_cast<int>(d.data.size()), 0,
                      reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (n >= 0) {
      d.data.resize(static_cast<size_t>(n));
      d.from = {ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)};
      return d;
    }
#ifdef _WIN32
    if (WSAGetLastError() == WSAEWOULDBLOCK) return std::nullopt;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return std::nullopt;
#endif
  }
  return std::nullopt;
}

uint16_t UdpSocket::localPort() const {
  sockaddr_in addr{};
  socklen_t len = sizeof addr;
  if (getsockname(raw(handle_), reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
  return ntohs(addr.sin_port);
}

}  // namespace coop
