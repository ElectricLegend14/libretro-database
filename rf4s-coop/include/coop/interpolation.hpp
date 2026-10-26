#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <numbers>
#include <optional>

#include "coop/types.hpp"

namespace coop {

// Smooths something the other side owns (the partner, a monster) by drawing
// it slightly in the past and blending between the two snapshots around that
// moment. T needs `mapId`, `pos` and `facing`; other fields come from the
// nearer snapshot.
template <typename T>
class Interpolated {
 public:
  static constexpr size_t kMaxBuffered = 32;
  // A tick this far behind the newest one means the sender restarted.
  static constexpr uint32_t kRestartGap = 100;

  Interpolated(double tickRate, double delayTicks) : tickRate_(tickRate), delayTicks_(delayTicks) {}

  void push(uint32_t tick, const T& body, double now) {
    if (!states_.empty() && tick + kRestartGap < states_.back().tick) states_.clear();
    auto at = std::lower_bound(states_.begin(), states_.end(), tick,
                               [](const Entry& e, uint32_t t) { return e.tick < t; });
    if (at != states_.end() && at->tick == tick) return;  // duplicate
    if (at == states_.end()) latestAt_ = now;
    states_.insert(at, Entry{tick, body});
    while (states_.size() > kMaxBuffered) states_.pop_front();
  }

  std::optional<T> view(double now) const {
    if (states_.empty()) return std::nullopt;
    double estimatedTick = states_.back().tick + (now - latestAt_) * tickRate_;
    return sample(estimatedTick - delayTicks_);
  }

  std::optional<T> sample(double tick) const {
    if (states_.empty()) return std::nullopt;
    if (tick <= states_.front().tick) return states_.front().body;
    if (tick >= states_.back().tick) return states_.back().body;  // never extrapolate

    auto after = std::upper_bound(states_.begin(), states_.end(), tick,
                                  [](double t, const Entry& e) { return t < e.tick; });
    auto before = std::prev(after);
    const T& a = before->body;
    const T& b = after->body;
    if (a.mapId != b.mapId) return b;  // don't slide across a map change

    double f = (tick - before->tick) / static_cast<double>(after->tick - before->tick);
    T out = f < 0.5 ? a : b;
    out.pos = {lerp(a.pos.x, b.pos.x, f), lerp(a.pos.y, b.pos.y, f), lerp(a.pos.z, b.pos.z, f)};
    out.facing = lerpAngle(a.facing, b.facing, f);
    return out;
  }

  // The newest snapshot, undelayed: what the owner last reported.
  std::optional<T> latest() const {
    if (states_.empty()) return std::nullopt;
    return states_.back().body;
  }

  void reset() { states_.clear(); }

 private:
  struct Entry {
    uint32_t tick;
    T body;
  };

  static float lerp(float a, float b, double f) { return static_cast<float>(a + (b - a) * f); }
  static float lerpAngle(float a, float b, double f) {
    double delta = std::remainder(static_cast<double>(b) - a, 2 * std::numbers::pi);
    return static_cast<float>(a + delta * f);
  }

  double tickRate_;
  double delayTicks_;
  double latestAt_ = 0;
  std::deque<Entry> states_;  // sorted by tick
};

}  // namespace coop
