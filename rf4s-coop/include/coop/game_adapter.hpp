#pragma once

#include <optional>
#include <string>
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

  // Every tile of every farm field the host owns.
  virtual std::vector<FarmTile> readFarmTiles() { return {}; }

  // Runs the guest's tool or seed on the host's farm with the game's own
  // rules. The host checks reach; the tile change itself is picked up by the
  // next readFarmTiles.
  virtual FarmOutcome applyGuestFarmAction(const FarmIntent&, const PlayerSnapshot& /*guest*/) { return {}; }

  // ---- Both sides: items --------------------------------------------------

  // Removes `count` of an item to hand to the partner. False (and nothing
  // removed) if there aren't enough.
  virtual bool takeItems(uint16_t /*itemId*/, uint16_t /*count*/) { return false; }
  virtual void giveItems(uint16_t itemId, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) giveItem(itemId);
  }

  // ---- Guest only ----------------------------------------------------------

  // While connected, the guest's own game must not run its own monsters,
  // drops or farm; the host's copies are shown instead.
  virtual void setLocalWorldSuppressed(bool /*suppressed*/) {}

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
  virtual void takeItem(uint16_t /*itemId*/) {}

  // Overwrites the guest's view of the host's farm.
  virtual void applyFarmTiles(const std::vector<FarmTile>&) {}
  virtual std::vector<FarmIntent> drainLocalFarmActions() { return {}; }

  // The guest's character file: read it out of the game to save it, and put
  // it back into the game after loading. nullopt = not supported yet.
  virtual std::optional<GuestCharacter> readGuestCharacter() { return std::nullopt; }
  virtual void applyGuestCharacter(const GuestCharacter&) {}

  // While the host is in a cutscene the guest is frozen and told why.
  virtual void freezeLocalPlayer(bool /*frozen*/) {}
  virtual void showBanner(const std::string& /*text*/) {}  // empty hides it

  // After fainting, the guest wakes beside the host.
  virtual void reviveLocalPlayer(uint16_t mapId, const Vec3& pos, uint32_t /*hp*/) { warpLocalPlayer(mapId, pos); }
};

}  // namespace coop
