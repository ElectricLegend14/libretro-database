#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <random>
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

// Anything that moves datagrams: a real UDP socket, or a wrapper that makes
// the network worse on purpose for testing.
class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual bool sendTo(const Endpoint& to, const std::vector<uint8_t>& data) = 0;
  // Returns nullopt when nothing is waiting. Never blocks.
  virtual std::optional<Datagram> receive() = 0;
  virtual uint16_t localPort() const = 0;
  // Called once per frame with the session's clock; most transports ignore it.
  virtual void advance(double /*now*/) {}
};

struct NetworkConditions {
  double latencySec = 0;    // one way, added to every packet
  double jitterSec = 0;     // extra random delay in [0, jitter], which also reorders
  double lossRate = 0;      // 0..1
  double duplicateRate = 0; // 0..1
  uint32_t seed = 1;
};

// Wraps another transport and degrades what it sends: delay, jitter
// (reordering), loss and duplicates. Deterministic for a given seed.
class ConditionedTransport : public ITransport {
 public:
  ConditionedTransport(std::unique_ptr<ITransport> inner, NetworkConditions conditions)
      : inner_(std::move(inner)), cond_(conditions), rng_(conditions.seed) {}

  bool sendTo(const Endpoint& to, const std::vector<uint8_t>& data) override;
  std::optional<Datagram> receive() override { return inner_->receive(); }
  uint16_t localPort() const override { return inner_->localPort(); }
  void advance(double now) override;

 private:
  struct Delayed {
    double due;
    uint64_t order;
    Endpoint to;
    std::vector<uint8_t> data;
    bool operator>(const Delayed& o) const { return due != o.due ? due > o.due : order > o.order; }
  };

  void schedule(const Endpoint& to, const std::vector<uint8_t>& data);

  std::unique_ptr<ITransport> inner_;
  NetworkConditions cond_;
  std::mt19937 rng_;
  double now_ = 0;
  uint64_t order_ = 0;
  std::priority_queue<Delayed, std::vector<Delayed>, std::greater<>> queue_;
};

}  // namespace coop
