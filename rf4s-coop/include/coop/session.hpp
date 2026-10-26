#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "coop/game_adapter.hpp"
#include "coop/interpolation.hpp"
#include "coop/protocol.hpp"
#include "coop/transport.hpp"

namespace coop {

constexpr uint16_t kModVersion = 1;

struct SessionConfig {
  uint16_t port = 7777;          // host: port to listen on; guest: host's port
  std::string hostAddress;       // guest only
  std::string playerName = "Player";
  uint32_t gameBuild = 0;        // both sides must match
  double tickRate = 20;          // state packets per second
  double interpDelayTicks = 2;   // draw the partner this far in the past
  double timeoutSec = 10;        // long enough to ride out a loading screen
  double leashSec = 10;          // guest only
  std::function<void(const std::string&)> log;
  std::function<void()> onHostSleep;  // guest only: the host went to bed (save now)
};

// The partner, smoothed. See Interpolated.
class RemotePlayer : public Interpolated<PlayerSnapshot> {
 public:
  using Interpolated::Interpolated;
  void onState(const PlayerState& state, double now) { push(state.tick, state.body, now); }
};

class Session {
 public:
  virtual ~Session() = default;
  // Call once per frame with a monotonic time in seconds.
  virtual void update(double now) = 0;
  virtual bool connected() const = 0;
  virtual void sendChat(const std::string& text) = 0;
  // Takes the items from the local inventory and hands them to the partner.
  // If the link drops before the partner confirms, they come back.
  virtual bool giveToPartner(uint16_t itemId, uint16_t count) = 0;
  virtual void leave() = 0;
};

// Host: the authority. Accepts one guest and sends its own state and the clock.
// Opens a UDP socket on config.port; returns nullptr if it can't.
std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config);
std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config, std::unique_ptr<ITransport> transport);

// Guest: follows the host. `failed` turns true when the host refuses the guest
// or ends the session; after that it stops trying to reconnect.
class GuestSession : public Session {
 public:
  virtual bool failed() const = 0;
};
// Resolves config.hostAddress and opens a UDP socket; returns nullptr if it can't.
std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config);
std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config,
                                          std::unique_ptr<ITransport> transport, Endpoint host);

}  // namespace coop
