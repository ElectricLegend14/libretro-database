#pragma once

#include <cmath>
#include <cstdint>

namespace coop {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

inline float distance(const Vec3& a, const Vec3& b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Everything one player looks like from the outside. The game adapter reads
// this for the local player, and the remote player is drawn from it.
struct PlayerSnapshot {
  uint16_t mapId = 0;
  Vec3 pos;
  float facing = 0;  // radians
  uint16_t anim = 0;
  uint32_t hp = 0, maxHp = 0;
  uint32_t rp = 0, maxRp = 0;
};

struct GameClock {
  uint16_t year = 1;
  uint8_t season = 0;  // 0 spring, 1 summer, 2 autumn, 3 winter
  uint8_t day = 1;
  uint8_t hour = 6;
  uint8_t minute = 0;

  bool operator==(const GameClock&) const = default;
};

enum class EntityCategory : uint8_t { Monster = 0, Drop = 1, Npc = 2 };

// A monster, item drop or NPC as the host sees it. `id` is the host's handle
// for it; the guest never invents IDs.
struct EntityState {
  uint32_t id = 0;
  EntityCategory category = EntityCategory::Monster;
  uint16_t kind = 0;  // species, item or NPC ID
  uint16_t mapId = 0;
  Vec3 pos;
  float facing = 0;
  uint16_t anim = 0;
  uint32_t hp = 0, maxHp = 0;
};

enum class AttackKind : uint8_t { Melee = 0, Spell = 1, Tool = 2 };

// The guest pressed attack. The host decides what, if anything, it hits.
struct AttackIntent {
  AttackKind kind = AttackKind::Melee;
  uint16_t itemId = 0;  // weapon, spell or tool
  Vec3 origin;
  float facing = 0;
  uint32_t targetHint = 0;  // lock-on target, 0 for none
};

// One target hit by an attack, as settled by the host.
struct Hit {
  uint32_t targetId = 0;
  uint32_t damage = 0;
  uint32_t hpAfter = 0;
  bool killed = false;
};

// A monster hit the guest; decided by the host, applied by the guest.
struct HurtEvent {
  uint32_t attackerId = 0;
  uint32_t damage = 0;
  Vec3 knockback;
};

enum class Soil : uint8_t { Untilled = 0, Tilled = 1, Watered = 2 };

// One square of a farm field. The host owns every tile; the guest sees copies.
struct FarmTile {
  uint16_t fieldId = 0;
  uint8_t x = 0, y = 0;
  Soil soil = Soil::Untilled;
  uint16_t cropId = 0;  // 0 = nothing planted
  uint8_t growth = 0;

  uint32_t key() const { return (static_cast<uint32_t>(fieldId) << 16) | (x << 8) | y; }
  bool operator==(const FarmTile&) const = default;
};

enum class FarmAction : uint8_t { Till = 0, Water = 1, Plant = 2, Harvest = 3 };

// The guest used a tool or seed on a tile. The host decides if it works.
struct FarmIntent {
  FarmAction action = FarmAction::Till;
  uint16_t fieldId = 0;
  uint8_t x = 0, y = 0;
  uint16_t itemId = 0;  // seed for Plant, tool otherwise
};

struct FarmOutcome {
  bool ok = false;
  uint16_t harvestedItem = 0;  // set by a successful Harvest
};

}  // namespace coop
