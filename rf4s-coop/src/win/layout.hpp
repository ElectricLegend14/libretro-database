#pragma once

// Where things live inside Rune Factory 4 Special. Every value here comes from
// reverse engineering (see "Reverse-engineering plan" in the design doc) and
// none is known yet. Fill them in as they're found, one target at a time, and
// check each new signature with the sigscan tool first: it must match exactly
// once in the game executable.

#include <cstddef>

namespace coop::rf4s {

// A global pointer reached through an x64 RIP-relative instruction such as
// `48 8B 05 xx xx xx xx` (mov rax, [rip+disp32]).
struct GlobalRef {
  const char* signature;  // IDA-style pattern for the instruction; nullptr = not found yet
  size_t dispOffset;      // where disp32 sits inside the instruction
  size_t instrLength;     // size of the whole instruction
};

constexpr ptrdiff_t kUnknown = -1;

// RE target 1-2: the player object.
constexpr GlobalRef kPlayerPtr{nullptr, 3, 7};

struct PlayerOffsets {
  ptrdiff_t posX = kUnknown;  // three consecutive floats: x, y, z
  ptrdiff_t facing = kUnknown;
  ptrdiff_t anim = kUnknown;  // uint16
  ptrdiff_t hp = kUnknown;    // uint32 each
  ptrdiff_t maxHp = kUnknown;
  ptrdiff_t rp = kUnknown;
  ptrdiff_t maxRp = kUnknown;
};
constexpr PlayerOffsets kPlayer{};

// RE target 6: the current map. Points straight at a uint16 map ID.
constexpr GlobalRef kMapId{nullptr, 2, 6};

// RE target 7: the calendar. Points at the clock structure.
constexpr GlobalRef kClockPtr{nullptr, 3, 7};

struct ClockOffsets {
  ptrdiff_t year = kUnknown;    // uint16
  ptrdiff_t season = kUnknown;  // uint8 each
  ptrdiff_t day = kUnknown;
  ptrdiff_t hour = kUnknown;
  ptrdiff_t minute = kUnknown;
};
constexpr ClockOffsets kClock{};

}  // namespace coop::rf4s
