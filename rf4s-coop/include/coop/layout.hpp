#pragma once

#include <cstddef>
#include <string>

namespace coop {

// Where things live inside the game: signatures for the globals and offsets
// inside the structures they point to. Compiled-in defaults come from
// src/win/layout.hpp; rf4s-layout.ini next to the DLL overrides them at
// startup, so a guess can be tried without rebuilding.

constexpr ptrdiff_t kUnknownOffset = -1;

// A global pointer reached through an x64 RIP-relative instruction such as
// `48 8B 05 xx xx xx xx` (mov rax, [rip+disp32]).
struct GlobalRef {
  std::string signature;  // IDA-style pattern for the instruction; empty = not found yet
  size_t dispOffset = 3;  // where disp32 sits inside the matched bytes
  size_t instrLength = 7; // from the start of the match to the end of the instruction
};

// A function found by a signature: its start is the match plus `offset`.
struct FunctionRef {
  std::string signature;  // empty = not found yet
  ptrdiff_t offset = 0;   // may be negative when the pattern starts inside the function
};

struct PlayerOffsets {
  ptrdiff_t pos = kUnknownOffset;  // three consecutive floats: x, y, z
  ptrdiff_t facing = kUnknownOffset;
  ptrdiff_t anim = kUnknownOffset;  // uint16
  ptrdiff_t hp = kUnknownOffset;    // uint32 each
  ptrdiff_t maxHp = kUnknownOffset;
  ptrdiff_t rp = kUnknownOffset;
  ptrdiff_t maxRp = kUnknownOffset;
};

struct ClockOffsets {
  ptrdiff_t year = kUnknownOffset;  // uint16
  ptrdiff_t season = kUnknownOffset;  // uint8 each
  ptrdiff_t day = kUnknownOffset;
  ptrdiff_t hour = kUnknownOffset;
  ptrdiff_t minute = kUnknownOffset;
};

struct GameLayout {
  GlobalRef player;              // RE targets 1-2: points at the player object
  GlobalRef mapId{"", 2, 6};     // RE target 6: points straight at a uint16 map ID
  GlobalRef clock;               // RE target 7: points at the calendar structure
  FunctionRef frame;             // the game's per-frame update: co-op runs inside it
  PlayerOffsets playerFields;
  ClockOffsets clockFields;
  size_t dumpPlayerBytes = 0;    // > 0: log a hex dump of the player object every 2 s
};

// Applies `key = value` lines from rf4s-layout.ini on top of `layout`.
// Numbers may be decimal or 0x-hex. Keys:
//   player.signature, player.disp_offset, player.instr_length (and map.*, clock.*)
//   frame.signature, frame.offset
//   player.pos, player.facing, player.anim, player.hp, player.max_hp, player.rp, player.max_rp
//   clock.year, clock.season, clock.day, clock.hour, clock.minute
//   dump_player_bytes
// Returns false with `error` set on an unknown key or a bad value.
bool applyLayoutOverrides(const std::string& text, GameLayout& layout, std::string& error);

}  // namespace coop
