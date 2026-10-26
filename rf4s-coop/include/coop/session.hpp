#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "coop/game_adapter.hpp"
#include "coop/protocol.hpp"
#include "coop/udp.hpp"

namespace coop {

constexpr uint16_t kModVersion = 1;

struct SessionConfig {
  uint16_t port = 7777;          // host: port to listen on; guest: host's port
  std::string hostAddress;       // guest only
  std::string playerName = "Player";
  uint32_t gameBuild = 0;        // both sides must match
  double tickRate = 20;          // state packets per second
  double interpDelayTicks = 2;   // draw the partner this far in the past
  double timeoutSec = 5;
  double leashSec = 10;          // guest only
  std::function<void(const std::string&)> log;
};

// Smooths the partner's movement by drawing them slightly in the past and
// blending between the two snapshots around that moment.
class RemotePlayer {
 public:
  RemotePlayer(double tickRate, double delayTicks) : tickRate_(tickRate), delayTicks_(delayTicks) {}

  void onState(const PlayerState& state, double now);
  std::optional<PlayerSnapshot> view(double now) const;
  std::optional<PlayerSnapshot> sample(double tick) const;
  void reset() { states_.clear(); }

 private:
  double tickRate_;
  double delayTicks_;
  double latestAt_ = 0;
  std::deque<PlayerState> states_;  // sorted by tick
};

class Session {
 public:
  virtual ~Session() = default;
  // Call once per frame with a monotonic time in seconds.
  virtual void update(double now) = 0;
  virtual bool connected() const = 0;
  virtual void sendChat(const std::string& text) = 0;
  virtual void leave() = 0;
};

// Host: the authority. Accepts one guest and sends its own state and the clock.
std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config);

// Guest: follows the host. Reports `failed` when the host rejects it.
class GuestSession : public Session {
 public:
  virtual bool failed() const = 0;
};
std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config);

}  // namespace coop
