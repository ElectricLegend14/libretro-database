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
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMaxString = 256;

enum class MsgType : uint8_t {
  Hello = 0x01,
  Welcome = 0x02,
  PlayerState = 0x10,
  ClockSync = 0x31,
  Chat = 0x40,
  Goodbye = 0xFF,
};

struct Hello {
  static constexpr MsgType kType = MsgType::Hello;
  uint16_t modVersion = 0;
  uint32_t gameBuild = 0;
  std::string name;
};

struct Welcome {
  static constexpr MsgType kType = MsgType::Welcome;
  bool accepted = false;
  uint8_t slot = 0;
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

std::vector<uint8_t> encode(const Message& msg);

// Returns nullopt for anything malformed: wrong version, unknown type,
// truncated or trailing bytes, or non-finite floats.
std::optional<Message> decode(const uint8_t* data, size_t len);

}  // namespace coop
