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
  ClockSync = 0x31,
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

using Message = std::variant<Hello, Welcome, PlayerState, ClockSync, Chat, Goodbye>;

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
