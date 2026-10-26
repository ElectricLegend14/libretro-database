#pragma once

#include <cstdint>

namespace coop {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

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

}  // namespace coop
