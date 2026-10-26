#include "coop/transport.hpp"

namespace coop {

std::string Endpoint::toString() const {
  return std::to_string(ipv4 >> 24) + "." + std::to_string((ipv4 >> 16) & 0xFF) + "." +
         std::to_string((ipv4 >> 8) & 0xFF) + "." + std::to_string(ipv4 & 0xFF) + ":" +
         std::to_string(port);
}

bool ConditionedTransport::sendTo(const Endpoint& to, const std::vector<uint8_t>& data) {
  std::uniform_real_distribution<double> chance(0, 1);
  if (chance(rng_) < cond_.lossRate) return true;  // lost on the way; the sender can't tell
  schedule(to, data);
  if (chance(rng_) < cond_.duplicateRate) schedule(to, data);
  advance(now_);  // with no delay configured, send at once
  return true;
}

void ConditionedTransport::schedule(const Endpoint& to, const std::vector<uint8_t>& data) {
  std::uniform_real_distribution<double> jitter(0, cond_.jitterSec);
  queue_.push({now_ + cond_.latencySec + jitter(rng_), order_++, to, data});
}

void ConditionedTransport::advance(double now) {
  now_ = now;
  while (!queue_.empty() && queue_.top().due <= now_) {
    inner_->sendTo(queue_.top().to, queue_.top().data);
    queue_.pop();
  }
}

}  // namespace coop
