#include "coop/sim_game.hpp"

#include <cmath>
#include <numbers>

namespace coop {

void SimGame::advance(double dt) {
  time_ += dt;
  player_.pos = positionAt(time_);
  player_.facing = static_cast<float>(std::remainder(speed_ * time_ + std::numbers::pi / 2, 2 * std::numbers::pi));
  player_.anim = 1;  // walking

  // One game minute per real second, which is close enough for testing.
  clockCarry_ += dt;
  while (clockCarry_ >= 1) {
    clockCarry_ -= 1;
    if (++clock_.minute < 60) continue;
    clock_.minute = 0;
    if (++clock_.hour < 24) continue;
    clock_.hour = 0;
    if (++clock_.day <= 30) continue;
    clock_.day = 1;
    if (++clock_.season < 4) continue;
    clock_.season = 0;
    ++clock_.year;
  }
}

Vec3 SimGame::positionAt(double time) const {
  double angle = speed_ * time;
  return {offset_.x + static_cast<float>(radius_ * std::cos(angle)), offset_.y,
          offset_.z + static_cast<float>(radius_ * std::sin(angle))};
}

void SimGame::warpLocalPlayer(uint16_t mapId, const Vec3& pos) {
  player_.mapId = mapId;
  Vec3 onCircle = positionAt(time_);
  offset_ = {offset_.x + pos.x - onCircle.x, offset_.y + pos.y - onCircle.y, offset_.z + pos.z - onCircle.z};
  player_.pos = pos;
  ++warps_;
}

}  // namespace coop
