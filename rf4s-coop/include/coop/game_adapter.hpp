#pragma once

#include "coop/types.hpp"

namespace coop {

// The only way the sync code touches the game. The real implementation reads
// and writes RF4S memory (src/win/rf4s_adapter.cpp); SimGame stands in for it
// in tests and in the coop_sim tool.
class IGameAdapter {
 public:
  virtual ~IGameAdapter() = default;

  virtual PlayerSnapshot readLocalPlayer() = 0;
  virtual GameClock readClock() = 0;
  virtual void writeClock(const GameClock& clock) = 0;

  // Called every frame while the partner is on the same map.
  virtual void showRemotePlayer(const PlayerSnapshot& remote) = 0;
  virtual void hideRemotePlayer() = 0;

  // Leash: move the local player to the partner's map.
  virtual void warpLocalPlayer(uint16_t mapId, const Vec3& pos) = 0;
};

}  // namespace coop
