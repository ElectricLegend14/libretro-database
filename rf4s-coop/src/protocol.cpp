#include "coop/protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace coop {
namespace {

// All multi-byte values are little-endian on the wire.
class Writer {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v));
    u8(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v));
    u16(static_cast<uint16_t>(v >> 16));
  }
  void f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    u32(bits);
  }
  void str(const std::string& s) {
    size_t n = std::min(s.size(), kMaxString);
    u16(static_cast<uint16_t>(n));
    buf_.insert(buf_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
  }
  void vec3(const Vec3& v) {
    f32(v.x);
    f32(v.y);
    f32(v.z);
  }
  std::vector<uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<uint8_t> buf_;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

  bool u8(uint8_t& v) {
    if (pos_ >= len_) return false;
    v = data_[pos_++];
    return true;
  }
  bool u16(uint16_t& v) {
    uint8_t lo, hi;
    if (!u8(lo) || !u8(hi)) return false;
    v = static_cast<uint16_t>(lo | (hi << 8));
    return true;
  }
  bool u32(uint32_t& v) {
    uint16_t lo, hi;
    if (!u16(lo) || !u16(hi)) return false;
    v = lo | (static_cast<uint32_t>(hi) << 16);
    return true;
  }
  bool f32(float& v) {
    uint32_t bits;
    if (!u32(bits)) return false;
    std::memcpy(&v, &bits, sizeof v);
    return std::isfinite(v);
  }
  bool str(std::string& s) {
    uint16_t n;
    if (!u16(n) || n > kMaxString || len_ - pos_ < n) return false;
    s.assign(reinterpret_cast<const char*>(data_ + pos_), n);
    pos_ += n;
    return true;
  }
  bool vec3(Vec3& v) { return f32(v.x) && f32(v.y) && f32(v.z); }
  bool done() const { return pos_ == len_; }

 private:
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
};

void write(Writer& w, const Hello& m) {
  w.u16(m.modVersion);
  w.u32(m.gameBuild);
  w.u32(m.nonce);
  w.str(m.name);
}
void write(Writer& w, const Welcome& m) {
  w.u8(m.accepted ? 1 : 0);
  w.u8(m.slot);
  w.u32(m.nonce);
  w.str(m.reason);
}
void write(Writer& w, const PlayerState& m) {
  w.u32(m.tick);
  w.u16(m.body.mapId);
  w.vec3(m.body.pos);
  w.f32(m.body.facing);
  w.u16(m.body.anim);
  w.u32(m.body.hp);
  w.u32(m.body.maxHp);
  w.u32(m.body.rp);
  w.u32(m.body.maxRp);
}
void write(Writer& w, const AttackIntent& a) {
  w.u8(static_cast<uint8_t>(a.kind));
  w.u16(a.itemId);
  w.vec3(a.origin);
  w.f32(a.facing);
  w.u32(a.targetHint);
}
void write(Writer& w, const EntityState& e) {
  w.u32(e.id);
  w.u8(static_cast<uint8_t>(e.category));
  w.u16(e.kind);
  w.u16(e.mapId);
  w.vec3(e.pos);
  w.f32(e.facing);
  w.u16(e.anim);
  w.u32(e.hp);
  w.u32(e.maxHp);
}
void write(Writer& w, const AttackRequest& m) {
  w.u32(m.requestId);
  write(w, m.intent);
}
void write(Writer& w, const PickupRequest& m) { w.u32(m.entityId); }
void write(Writer& w, const EntitySpawn& m) {
  w.u32(m.tick);
  write(w, m.entity);
}
void write(Writer& w, const EntityUpdates& m) {
  size_t n = std::min(m.entities.size(), EntityUpdates::kMaxPerPacket);
  w.u32(m.tick);
  w.u8(static_cast<uint8_t>(n));
  for (size_t i = 0; i < n; ++i) {
    const auto& e = m.entities[i];
    w.u32(e.id);
    w.vec3(e.pos);
    w.f32(e.facing);
    w.u16(e.anim);
    w.u32(e.hp);
  }
}
void write(Writer& w, const EntityDespawn& m) {
  w.u32(m.id);
  w.u8(static_cast<uint8_t>(m.reason));
}
void write(Writer& w, const HitResult& m) {
  size_t n = std::min(m.hits.size(), HitResult::kMaxHits);
  w.u32(m.requestId);
  w.u8(static_cast<uint8_t>(n));
  for (size_t i = 0; i < n; ++i) {
    const auto& h = m.hits[i];
    w.u32(h.targetId);
    w.u32(h.damage);
    w.u32(h.hpAfter);
    w.u8(h.killed ? 1 : 0);
  }
}
void write(Writer& w, const PlayerHurt& m) {
  w.u32(m.hurt.attackerId);
  w.u32(m.hurt.damage);
  w.vec3(m.hurt.knockback);
}
void write(Writer& w, const PickupResult& m) {
  w.u32(m.entityId);
  w.u8(m.granted ? 1 : 0);
  w.u16(m.itemId);
}
void write(Writer& w, const ClockSync& m) {
  w.u16(m.clock.year);
  w.u8(m.clock.season);
  w.u8(m.clock.day);
  w.u8(m.clock.hour);
  w.u8(m.clock.minute);
}
void write(Writer& w, const Chat& m) { w.str(m.text); }
void write(Writer& w, const Goodbye& m) { w.u8(static_cast<uint8_t>(m.reason)); }

bool read(Reader& r, Hello& m) {
  return r.u16(m.modVersion) && r.u32(m.gameBuild) && r.u32(m.nonce) && r.str(m.name);
}
bool readBool(Reader& r, bool& out);
bool read(Reader& r, Welcome& m) { return readBool(r, m.accepted) && r.u8(m.slot) && r.u32(m.nonce) && r.str(m.reason); }
bool read(Reader& r, PlayerState& m) {
  return r.u32(m.tick) && r.u16(m.body.mapId) && r.vec3(m.body.pos) && r.f32(m.body.facing) &&
         r.u16(m.body.anim) && r.u32(m.body.hp) && r.u32(m.body.maxHp) && r.u32(m.body.rp) &&
         r.u32(m.body.maxRp);
}
bool readBool(Reader& r, bool& out) {
  uint8_t v;
  if (!r.u8(v) || v > 1) return false;
  out = v == 1;
  return true;
}
template <typename E>
bool readEnum(Reader& r, E& out, E last) {
  uint8_t v;
  if (!r.u8(v) || v > static_cast<uint8_t>(last)) return false;
  out = static_cast<E>(v);
  return true;
}
bool read(Reader& r, AttackIntent& a) {
  return readEnum(r, a.kind, AttackKind::Tool) && r.u16(a.itemId) && r.vec3(a.origin) && r.f32(a.facing) &&
         r.u32(a.targetHint);
}
bool read(Reader& r, EntityState& e) {
  return r.u32(e.id) && readEnum(r, e.category, EntityCategory::Npc) && r.u16(e.kind) && r.u16(e.mapId) &&
         r.vec3(e.pos) && r.f32(e.facing) && r.u16(e.anim) && r.u32(e.hp) && r.u32(e.maxHp);
}
bool read(Reader& r, AttackRequest& m) { return r.u32(m.requestId) && read(r, m.intent); }
bool read(Reader& r, PickupRequest& m) { return r.u32(m.entityId); }
bool read(Reader& r, EntitySpawn& m) { return r.u32(m.tick) && read(r, m.entity); }
bool read(Reader& r, EntityUpdates& m) {
  uint8_t n;
  if (!r.u32(m.tick) || !r.u8(n) || n > EntityUpdates::kMaxPerPacket) return false;
  m.entities.resize(n);
  for (auto& e : m.entities) {
    if (!r.u32(e.id) || !r.vec3(e.pos) || !r.f32(e.facing) || !r.u16(e.anim) || !r.u32(e.hp)) return false;
  }
  return true;
}
bool read(Reader& r, EntityDespawn& m) { return r.u32(m.id) && readEnum(r, m.reason, DespawnReason::PickedUp); }
bool read(Reader& r, HitResult& m) {
  uint8_t n;
  if (!r.u32(m.requestId) || !r.u8(n) || n > HitResult::kMaxHits) return false;
  m.hits.resize(n);
  for (auto& h : m.hits) {
    if (!r.u32(h.targetId) || !r.u32(h.damage) || !r.u32(h.hpAfter) || !readBool(r, h.killed)) return false;
  }
  return true;
}
bool read(Reader& r, PlayerHurt& m) {
  return r.u32(m.hurt.attackerId) && r.u32(m.hurt.damage) && r.vec3(m.hurt.knockback);
}
bool read(Reader& r, PickupResult& m) { return r.u32(m.entityId) && readBool(r, m.granted) && r.u16(m.itemId); }
bool read(Reader& r, ClockSync& m) {
  return r.u16(m.clock.year) && r.u8(m.clock.season) && r.u8(m.clock.day) && r.u8(m.clock.hour) &&
         r.u8(m.clock.minute) && m.clock.season < 4 && m.clock.hour < 24 && m.clock.minute < 60;
}
bool read(Reader& r, Chat& m) { return r.str(m.text); }
bool read(Reader& r, Goodbye& m) { return readEnum(r, m.reason, GoodbyeReason::Timeout); }

template <typename T>
std::optional<Message> readAs(Reader& r) {
  T m;
  if (!read(r, m) || !r.done()) return std::nullopt;
  return Message{std::move(m)};
}

}  // namespace

std::vector<uint8_t> encodeMessage(const Message& msg) {
  Writer w;
  std::visit(
      [&w](const auto& m) {
        w.u8(static_cast<uint8_t>(m.kType));
        write(w, m);
      },
      msg);
  return w.take();
}

std::optional<Message> decodeMessage(const uint8_t* data, size_t len) {
  Reader r(data, len);
  uint8_t type;
  if (!r.u8(type)) return std::nullopt;
  switch (static_cast<MsgType>(type)) {
    case MsgType::Hello: return readAs<Hello>(r);
    case MsgType::Welcome: return readAs<Welcome>(r);
    case MsgType::PlayerState: return readAs<PlayerState>(r);
    case MsgType::AttackRequest: return readAs<AttackRequest>(r);
    case MsgType::PickupRequest: return readAs<PickupRequest>(r);
    case MsgType::EntitySpawn: return readAs<EntitySpawn>(r);
    case MsgType::EntityUpdates: return readAs<EntityUpdates>(r);
    case MsgType::EntityDespawn: return readAs<EntityDespawn>(r);
    case MsgType::HitResult: return readAs<HitResult>(r);
    case MsgType::PlayerHurt: return readAs<PlayerHurt>(r);
    case MsgType::PickupResult: return readAs<PickupResult>(r);
    case MsgType::ClockSync: return readAs<ClockSync>(r);
    case MsgType::Chat: return readAs<Chat>(r);
    case MsgType::Goodbye: return readAs<Goodbye>(r);
  }
  return std::nullopt;
}

std::vector<uint8_t> makePacket(PacketKind kind, uint16_t seq, const std::vector<uint8_t>& body) {
  Writer w;
  w.u8(kProtocolVersion);
  w.u8(static_cast<uint8_t>(kind));
  w.u16(seq);
  auto packet = w.take();
  packet.insert(packet.end(), body.begin(), body.end());
  return packet;
}

std::optional<PacketView> parsePacket(const uint8_t* data, size_t len) {
  Reader r(data, len);
  uint8_t version, kind;
  uint16_t seq;
  if (!r.u8(version) || version != kProtocolVersion || !r.u8(kind) || !r.u16(seq)) return std::nullopt;
  if (kind > static_cast<uint8_t>(PacketKind::Ack)) return std::nullopt;
  constexpr size_t kHeader = 4;
  PacketView view{static_cast<PacketKind>(kind), seq, data + kHeader, len - kHeader};
  if ((view.kind == PacketKind::Ack) != (view.bodyLen == 0)) return std::nullopt;
  return view;
}

}  // namespace coop
