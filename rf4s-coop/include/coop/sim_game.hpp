#pragma once

#include <optional>

#include "coop/game_adapter.hpp"

namespace coop {

// A stand-in for the real game: the player walks in a circle and the clock
// runs. Lets host and guest be developed and tested without RF4S running.
class SimGame : public IGameAdapter {
 public:
  SimGame(uint16_t mapId, float radius, float angularSpeed) : radius_(radius), speed_(angularSpeed) {
    player_.mapId = mapId;
    player_.hp = player_.maxHp = 100;
    player_.rp = player_.maxRp = 50;
    advance(0);
  }

  void advance(double dt);
  // Where the walking player was at a given simulated time.
  Vec3 positionAt(double time) const;
  double time() const { return time_; }

  PlayerSnapshot readLocalPlayer() override { return player_; }
  GameClock readClock() override { return clock_; }
  void writeClock(const GameClock& clock) override { clock_ = clock; }
  void showRemotePlayer(const PlayerSnapshot& remote) override { remote_ = remote; }
  void hideRemotePlayer() override { remote_.reset(); }
  void warpLocalPlayer(uint16_t mapId, const Vec3& pos) override;

  const std::optional<PlayerSnapshot>& remote() const { return remote_; }
  int warps() const { return warps_; }

 private:
  float radius_;
  float speed_;
  double time_ = 0;
  double clockCarry_ = 0;
  Vec3 offset_;
  PlayerSnapshot player_;
  GameClock clock_;
  std::optional<PlayerSnapshot> remote_;
  int warps_ = 0;
};

}  // namespace coop
