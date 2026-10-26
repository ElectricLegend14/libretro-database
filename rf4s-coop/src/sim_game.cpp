#include "coop/sim_game.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace coop {

void SimGame::advance(double dt) {
  time_ += dt;
  if (walking_) {
    player_.pos = positionAt(time_);
    player_.facing =
        static_cast<float>(std::remainder(speed_ * time_ + std::numbers::pi / 2, 2 * std::numbers::pi));
    player_.anim = 1;  // walking
  } else {
    player_.anim = 0;
  }

  // Monsters sway around their spawn point and bite the guest's body when
  // it's within reach, at most once a second each.
  for (auto& [id, m] : monsters_) {
    auto& e = world_.at(id);
    e.pos.x = m.home.x + static_cast<float>(std::sin(time_ * 0.5 + id));
    if (remote_ && remote_->mapId == e.mapId && distance(e.pos, remote_->pos) <= kBiteRange && time_ >= m.nextBite) {
      m.nextBite = time_ + 1;
      Vec3 push{remote_->pos.x - e.pos.x, 0, remote_->pos.z - e.pos.z};
      float len = std::max(0.001f, std::sqrt(push.x * push.x + push.z * push.z));
      hurts_.push_back({id, kBiteDamage, {push.x / len, 0, push.z / len}});
    }
  }

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

uint32_t SimGame::spawnMonster(uint16_t kind, const Vec3& pos, uint32_t hp) {
  uint32_t id = nextId_++;
  EntityState e;
  e.id = id;
  e.category = EntityCategory::Monster;
  e.kind = kind;
  e.mapId = player_.mapId;
  e.pos = pos;
  e.hp = e.maxHp = hp;
  world_[id] = e;
  monsters_[id] = {pos, 0};
  return id;
}

uint32_t SimGame::spawnDrop(uint16_t itemId, const Vec3& pos) {
  uint32_t id = nextId_++;
  EntityState e;
  e.id = id;
  e.category = EntityCategory::Drop;
  e.kind = itemId;
  e.mapId = player_.mapId;
  e.pos = pos;
  world_[id] = e;
  return id;
}

std::vector<EntityState> SimGame::readEntities() {
  std::vector<EntityState> out;
  for (auto& [id, e] : world_) {
    if (e.mapId == player_.mapId) out.push_back(e);
  }
  return out;
}

std::vector<Hit> SimGame::resolveGuestAttack(const AttackIntent& intent, const PlayerSnapshot& guest) {
  std::vector<Hit> hits;
  if (intent.kind == AttackKind::Tool) return hits;  // tools work the soil, not monsters
  float range = intent.kind == AttackKind::Spell ? kSpellRange : kMeleeRange;
  uint32_t damage = intent.kind == AttackKind::Spell ? kSpellDamage : kMeleeDamage;

  std::vector<uint32_t> killed;
  for (auto& [id, m] : monsters_) {
    auto& e = world_.at(id);
    if (e.mapId != guest.mapId || distance(e.pos, intent.origin) > range) continue;
    if (intent.kind == AttackKind::Spell && intent.targetHint != 0 && intent.targetHint != id) continue;
    uint32_t dealt = std::min(damage, e.hp);
    e.hp -= dealt;
    hits.push_back({id, dealt, e.hp, e.hp == 0});
    if (e.hp == 0) killed.push_back(id);
  }
  for (auto id : killed) {
    Vec3 where = world_.at(id).pos;
    uint16_t kind = world_.at(id).kind;
    world_.erase(id);
    monsters_.erase(id);
    spawnDrop(static_cast<uint16_t>(kDropItemBase + kind), where);
  }
  return hits;
}

std::vector<HurtEvent> SimGame::drainHitsOnRemotePlayer() { return std::exchange(hurts_, {}); }

std::optional<uint16_t> SimGame::takeDropForGuest(uint32_t entityId) {
  auto it = world_.find(entityId);
  if (it == world_.end() || it->second.category != EntityCategory::Drop) return std::nullopt;
  uint16_t item = it->second.kind;
  world_.erase(it);
  return item;
}

void SimGame::removeRemoteEntity(uint32_t id, bool killed) {
  mirrored_.erase(id);
  if (killed) killedSeen_.push_back(id);
}

void SimGame::applyHitToLocalPlayer(const HurtEvent& hurt) {
  player_.hp -= std::min(player_.hp, hurt.damage);
  player_.pos.x += hurt.knockback.x;
  player_.pos.z += hurt.knockback.z;
}

void SimGame::reviveLocalPlayer(uint16_t mapId, const Vec3& pos, uint32_t hp) {
  warpLocalPlayer(mapId, pos);
  player_.hp = hp;
}

}  // namespace coop
