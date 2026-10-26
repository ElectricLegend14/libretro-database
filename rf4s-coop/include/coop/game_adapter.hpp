#pragma once

#include <optional>
#include <vector>

#include "coop/types.hpp"

namespace coop {

// The only way the sync code touches the game. The real implementation reads
// and writes RF4S memory (src/win/rf4s_adapter.cpp); SimGame stands in for it
// in tests and in the coop_sim tool.
//
// Everything past the basics has a do-nothing default, so the real adapter
// can grow one reverse-engineered piece at a time.
class IGameAdapter {
 public:
  virtual ~IGameAdapter() = default;

  // ---- Both sides ----------------------------------------------------------

  virtual PlayerSnapshot readLocalPlayer() = 0;
  virtual GameClock readClock() = 0;
  virtual void writeClock(const GameClock& clock) = 0;

  // Called every frame while the partner is on the same map.
  virtual void showRemotePlayer(const PlayerSnapshot& remote) = 0;
  virtual void hideRemotePlayer() = 0;

  // Leash: move the local player to the partner's map.
  virtual void warpLocalPlayer(uint16_t mapId, const Vec3& pos) = 0;

  // ---- Host only -----------------------------------------------------------

  // Monsters, drops and NPCs near the host, on the host's map.
  virtual std::vector<EntityState> readEntities() { return {}; }

  // Runs the guest's attack in the host's world using the game's own damage
  // rules, applies the damage, and reports what was hit.
  virtual std::vector<Hit> resolveGuestAttack(const AttackIntent&, const PlayerSnapshot& /*guest*/) { return {}; }

  // Monster hits that landed on the guest's body since the last call.
  virtual std::vector<HurtEvent> drainHitsOnRemotePlayer() { return {}; }

  // Removes a drop from the world on the guest's behalf. Returns its item ID,
  // or nullopt if it's already gone.
  virtual std::optional<uint16_t> takeDropForGuest(uint32_t /*entityId*/) { return std::nullopt; }

  // ---- Guest only ----------------------------------------------------------

  // While connected, the guest's own game must not run monsters or drops;
  // the host's copies are shown instead.
  virtual void setLocalEntitiesSuppressed(bool /*suppressed*/) {}

  // Create or move the guest's copy of a host entity. Called every frame.
  virtual void upsertRemoteEntity(const EntityState&) {}
  virtual void removeRemoteEntity(uint32_t /*id*/, bool /*killed*/) {}

  // Attacks and pick-up presses by the local player since the last call.
  virtual std::vector<AttackIntent> drainLocalAttacks() { return {}; }
  virtual std::vector<uint32_t> drainLocalPickups() { return {}; }

  // Damage numbers and hit effects for an attack the host settled.
  virtual void showHits(const std::vector<Hit>&) {}

  virtual void applyHitToLocalPlayer(const HurtEvent&) {}
  virtual void giveItem(uint16_t /*itemId*/) {}

  // After fainting, the guest wakes beside the host.
  virtual void reviveLocalPlayer(uint16_t mapId, const Vec3& pos, uint32_t /*hp*/) { warpLocalPlayer(mapId, pos); }
};

}  // namespace coop
