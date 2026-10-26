#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "coop/game_adapter.hpp"

namespace coop {

// A stand-in for the real game: the player walks in a circle, the clock runs,
// monsters sway on the spot and bite whoever stands too close, and killed
// monsters leave a drop. Lets host and guest be developed and tested without
// RF4S running.
class SimGame : public IGameAdapter {
 public:
  static constexpr float kMeleeRange = 2.5f;
  static constexpr float kSpellRange = 8.f;
  static constexpr uint32_t kMeleeDamage = 10;
  static constexpr uint32_t kSpellDamage = 15;
  static constexpr float kBiteRange = 1.5f;
  static constexpr uint32_t kBiteDamage = 8;
  static constexpr uint16_t kDropItemBase = 1000;  // a monster of kind k drops item 1000 + k
  // The farm: one square field next to where the guest stands in tests.
  static constexpr uint16_t kFieldId = 1;
  static constexpr Vec3 kFieldOrigin{2, 0, -2};
  static constexpr float kFarmReach = 2.5f;
  static constexpr uint8_t kRipe = 3;               // days of watering before harvest
  static constexpr uint16_t kCropItemBase = 2000;  // seed s grows into item 2000 + s

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
  void setWalking(bool walking) { walking_ = walking; }

  // ---- world (host side)
  uint32_t spawnMonster(uint16_t kind, const Vec3& pos, uint32_t hp);
  uint32_t spawnDrop(uint16_t itemId, const Vec3& pos);
  // The host picks something up, or a monster wanders off.
  void removeEntity(uint32_t id) {
    world_.erase(id);
    monsters_.erase(id);
  }
  const std::map<uint32_t, EntityState>& world() const { return world_; }

  // ---- farm (host side)
  void createFarm(uint8_t size);
  // Works a tile with no reach check: what the host's own player does.
  FarmOutcome farmAction(const FarmIntent& intent);
  // A new day: watered crops grow one step and the soil dries out.
  void growCrops();
  Vec3 tileCenter(uint8_t x, uint8_t y) const {
    return {kFieldOrigin.x + x + 0.5f, kFieldOrigin.y, kFieldOrigin.z + y + 0.5f};
  }
  const std::map<uint32_t, FarmTile>& farm() const { return farm_; }

  // ---- what the guest side was asked to show and do
  void queueFarm(const FarmIntent& intent) { farmActions_.push_back(intent); }
  const std::map<uint32_t, FarmTile>& farmMirror() const { return farmMirror_; }
  void queueAttack(const AttackIntent& intent) { attacks_.push_back(intent); }
  void queuePickup(uint32_t id) { pickups_.push_back(id); }
  const std::map<uint32_t, EntityState>& mirrored() const { return mirrored_; }
  const std::vector<uint32_t>& killedSeen() const { return killedSeen_; }
  const std::vector<std::vector<Hit>>& hitsShown() const { return hitsShown_; }
  const std::vector<uint16_t>& inventory() const { return inventory_; }
  bool suppressed() const { return suppressed_; }
  const std::optional<PlayerSnapshot>& remote() const { return remote_; }
  int warps() const { return warps_; }
  void setHp(uint32_t hp) { player_.hp = hp; }
  void addItem(uint16_t itemId) { inventory_.push_back(itemId); }
  void setStatus(uint8_t status) { player_.status = status; }
  bool frozen() const { return frozen_; }
  const std::string& banner() const { return banner_; }

  // IGameAdapter
  PlayerSnapshot readLocalPlayer() override { return player_; }
  GameClock readClock() override { return clock_; }
  void writeClock(const GameClock& clock) override { clock_ = clock; }
  void showRemotePlayer(const PlayerSnapshot& remote) override { remote_ = remote; }
  void hideRemotePlayer() override { remote_.reset(); }
  void warpLocalPlayer(uint16_t mapId, const Vec3& pos) override;

  std::vector<EntityState> readEntities() override;
  std::vector<Hit> resolveGuestAttack(const AttackIntent& intent, const PlayerSnapshot& guest) override;
  std::vector<HurtEvent> drainHitsOnRemotePlayer() override;
  std::optional<uint16_t> takeDropForGuest(uint32_t entityId) override;
  std::vector<FarmTile> readFarmTiles() override;
  FarmOutcome applyGuestFarmAction(const FarmIntent& intent, const PlayerSnapshot& guest) override;

  void setLocalWorldSuppressed(bool suppressed) override { suppressed_ = suppressed; }
  void upsertRemoteEntity(const EntityState& e) override { mirrored_[e.id] = e; }
  void removeRemoteEntity(uint32_t id, bool killed) override;
  std::vector<AttackIntent> drainLocalAttacks() override { return std::exchange(attacks_, {}); }
  std::vector<uint32_t> drainLocalPickups() override { return std::exchange(pickups_, {}); }
  void showHits(const std::vector<Hit>& hits) override { hitsShown_.push_back(hits); }
  void applyHitToLocalPlayer(const HurtEvent& hurt) override;
  void giveItem(uint16_t itemId) override { inventory_.push_back(itemId); }
  void takeItem(uint16_t itemId) override;
  bool takeItems(uint16_t itemId, uint16_t count) override;
  void freezeLocalPlayer(bool frozen) override { frozen_ = frozen; }
  void showBanner(const std::string& text) override { banner_ = text; }
  void applyFarmTiles(const std::vector<FarmTile>& tiles) override;
  std::vector<FarmIntent> drainLocalFarmActions() override { return std::exchange(farmActions_, {}); }
  void reviveLocalPlayer(uint16_t mapId, const Vec3& pos, uint32_t hp) override;

 private:
  struct Monster {
    Vec3 home;
    double nextBite = 0;
  };

  float radius_;
  float speed_;
  bool walking_ = true;
  double time_ = 0;
  double clockCarry_ = 0;
  Vec3 offset_;
  PlayerSnapshot player_;
  GameClock clock_;
  std::optional<PlayerSnapshot> remote_;
  int warps_ = 0;

  uint32_t nextId_ = 100;
  std::map<uint32_t, EntityState> world_;
  std::map<uint32_t, Monster> monsters_;
  std::vector<HurtEvent> hurts_;
  std::map<uint32_t, FarmTile> farm_;

  bool suppressed_ = false;
  std::map<uint32_t, EntityState> mirrored_;
  std::vector<uint32_t> killedSeen_;
  std::vector<std::vector<Hit>> hitsShown_;
  std::vector<AttackIntent> attacks_;
  std::vector<uint32_t> pickups_;
  std::vector<uint16_t> inventory_;
  std::map<uint32_t, FarmTile> farmMirror_;
  std::vector<FarmIntent> farmActions_;
  bool frozen_ = false;
  std::string banner_;
};

}  // namespace coop
