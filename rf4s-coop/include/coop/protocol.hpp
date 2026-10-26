#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coop/types.hpp"

namespace coop {

// First byte of every packet. Bump on any change to the wire format.
constexpr uint8_t kProtocolVersion = 2;
constexpr size_t kMaxString = 256;

enum class MsgType : uint8_t {
  Hello = 0x01,
  Welcome = 0x02,
  PlayerState = 0x10,
  AttackRequest = 0x11,
  PickupRequest = 0x12,
  FarmRequest = 0x13,
  EntitySpawn = 0x20,
  EntityUpdates = 0x21,
  EntityDespawn = 0x22,
  HitResult = 0x23,
  PlayerHurt = 0x24,
  PickupResult = 0x25,
  FarmResult = 0x26,
  ClockSync = 0x31,
  FarmTiles = 0x32,
  Chat = 0x40,
  Goodbye = 0xFF,
};

// Hello and Welcome form the handshake and travel unreliably: the guest repeats
// Hello until a Welcome with its nonce comes back. A new nonce means a new
// connection, so a guest that restarts is never mistaken for the old one.
struct Hello {
  static constexpr MsgType kType = MsgType::Hello;
  uint16_t modVersion = 0;
  uint32_t gameBuild = 0;
  uint32_t nonce = 0;
  std::string name;
};

struct Welcome {
  static constexpr MsgType kType = MsgType::Welcome;
  bool accepted = false;
  uint8_t slot = 0;
  uint32_t nonce = 0;  // echoes the Hello
  std::string reason;  // set when rejected
};

struct PlayerState {
  static constexpr MsgType kType = MsgType::PlayerState;
  uint32_t tick = 0;
  PlayerSnapshot body;
};

// Guest -> host, reliable.
struct AttackRequest {
  static constexpr MsgType kType = MsgType::AttackRequest;
  uint32_t requestId = 0;
  AttackIntent intent;
};

// Guest -> host, reliable. First claim on a drop wins.
struct PickupRequest {
  static constexpr MsgType kType = MsgType::PickupRequest;
  uint32_t entityId = 0;
};

// Guest -> host, reliable.
struct FarmRequest {
  static constexpr MsgType kType = MsgType::FarmRequest;
  uint32_t requestId = 0;
  FarmIntent intent;
};

// Host -> guest, reliable. The tile itself changes through FarmTiles.
struct FarmResult {
  static constexpr MsgType kType = MsgType::FarmResult;
  uint32_t requestId = 0;
  FarmOutcome outcome;
};

// Host -> guest, reliable: tiles that changed (all of them right after joining).
struct FarmTiles {
  static constexpr MsgType kType = MsgType::FarmTiles;
  static constexpr size_t kMaxPerPacket = 120;
  std::vector<FarmTile> tiles;
};

// Host -> guest, reliable: an entity came into range.
struct EntitySpawn {
  static constexpr MsgType kType = MsgType::EntitySpawn;
  uint32_t tick = 0;
  EntityState entity;
};

// The fields that change every tick.
struct EntityDelta {
  uint32_t id = 0;
  Vec3 pos;
  float facing = 0;
  uint16_t anim = 0;
  uint32_t hp = 0;
};

// Host -> guest, unreliable, every tick. Updates for entities the guest hasn't
// been told about yet are ignored.
struct EntityUpdates {
  static constexpr MsgType kType = MsgType::EntityUpdates;
  static constexpr size_t kMaxPerPacket = 40;  // keeps the datagram under ~1100 bytes
  uint32_t tick = 0;
  std::vector<EntityDelta> entities;
};

enum class DespawnReason : uint8_t { Gone = 0, Killed = 1, PickedUp = 2 };

// Host -> guest, reliable.
struct EntityDespawn {
  static constexpr MsgType kType = MsgType::EntityDespawn;
  uint32_t id = 0;
  DespawnReason reason = DespawnReason::Gone;
};

// Host -> guest, reliable: what the guest's attack did. Empty `hits` = a miss.
struct HitResult {
  static constexpr MsgType kType = MsgType::HitResult;
  static constexpr size_t kMaxHits = 32;
  uint32_t requestId = 0;
  std::vector<Hit> hits;
};

// Host -> guest, reliable.
struct PlayerHurt {
  static constexpr MsgType kType = MsgType::PlayerHurt;
  HurtEvent hurt;
};

// Host -> guest, reliable.
struct PickupResult {
  static constexpr MsgType kType = MsgType::PickupResult;
  uint32_t entityId = 0;
  bool granted = false;
  uint16_t itemId = 0;
};

struct ClockSync {
  static constexpr MsgType kType = MsgType::ClockSync;
  GameClock clock;
};

struct Chat {
  static constexpr MsgType kType = MsgType::Chat;
  std::string text;
};

enum class GoodbyeReason : uint8_t { Quit = 0, Kicked = 1, Timeout = 2 };

struct Goodbye {
  static constexpr MsgType kType = MsgType::Goodbye;
  GoodbyeReason reason = GoodbyeReason::Quit;
};

using Message =
    std::variant<Hello, Welcome, PlayerState, AttackRequest, PickupRequest, FarmRequest, EntitySpawn, EntityUpdates,
                 EntityDespawn, HitResult, PlayerHurt, PickupResult, FarmResult, ClockSync, FarmTiles, Chat, Goodbye>;

// A message on its own: type byte + payload.
std::vector<uint8_t> encodeMessage(const Message& msg);

// Returns nullopt for anything malformed: unknown type, truncated or trailing
// bytes, out-of-range values or non-finite floats.
std::optional<Message> decodeMessage(const uint8_t* data, size_t len);

// Every datagram: [version][kind][seq u16][body]. Reliable packets carry one
// message and a sequence number; Ack packets carry only the sequence number
// they acknowledge; Unreliable packets carry one message and seq 0.
enum class PacketKind : uint8_t { Unreliable = 0, Reliable = 1, Ack = 2 };

struct PacketView {
  PacketKind kind;
  uint16_t seq;
  const uint8_t* body;
  size_t bodyLen;
};

std::vector<uint8_t> makePacket(PacketKind kind, uint16_t seq, const std::vector<uint8_t>& body);
std::optional<PacketView> parsePacket(const uint8_t* data, size_t len);

}  // namespace coop
