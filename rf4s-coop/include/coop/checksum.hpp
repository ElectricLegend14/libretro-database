#pragma once

#include <cstdint>
#include <map>

#include "coop/types.hpp"

namespace coop {

// 32-bit FNV-1a. Not cryptographic: it only has to notice when two copies of
// the world disagree.
class Fnv1a {
 public:
  void add(uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      hash_ ^= (v >> (8 * i)) & 0xFF;
      hash_ *= 16777619u;
    }
  }
  uint32_t value() const { return hash_; }

 private:
  uint32_t hash_ = 2166136261u;
};

// Both sides keep these maps sorted by key, so equal contents hash equally.
inline uint32_t hashFarm(const std::map<uint32_t, FarmTile>& tiles) {
  Fnv1a h;
  for (auto& [key, t] : tiles) {
    h.add(key);
    h.add(static_cast<uint32_t>(t.soil) | (static_cast<uint32_t>(t.growth) << 8) | (static_cast<uint32_t>(t.cropId) << 16));
  }
  return h.value();
}

// Which entities exist, not where they are: positions are always a little
// behind on the guest and are corrected every tick anyway.
template <typename Map, typename Get>
uint32_t hashEntities(const Map& entities, Get stateOf) {
  Fnv1a h;
  for (auto& [id, value] : entities) {
    const EntityState& e = stateOf(value);
    h.add(id);
    h.add(static_cast<uint32_t>(e.category) | (static_cast<uint32_t>(e.kind) << 8));
  }
  return h.value();
}

}  // namespace coop
