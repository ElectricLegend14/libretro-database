#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "coop/sigscan.hpp"

namespace coop {

// A game function the mod wants to hook, located through offsets.ini so that
// what you find in Ghidra can be tried without rebuilding the mod.
//
//   [hooks]
//   can_select_unit = 0x7A1B40                      ; offset into the main module
//   end_player_phase = pattern: FD 7B BF A9 ?? ?? 01 94
//   unit_character_id = call: ?? ?? ?? ?? E0 03 00 2A
//
// "pattern:" points at the start of the function. "call:" points at a BL
// instruction that calls the function; the mod follows the call. The first
// four bytes of a "call:" pattern always match any BL, so they can stay "??".
struct HookTarget {
    enum class Kind { Offset, Pattern, Call };

    std::string name;
    Kind kind = Kind::Offset;
    uint64_t offset = 0;  // Kind::Offset
    Pattern pattern;      // Kind::Pattern / Kind::Call
    int line = 0;
};

struct KnownHook {
    const char* name;
    const char* whatToFind;
};

// Every hook name the mod understands, with a hint for finding it.
const std::vector<KnownHook>& knownHooks();

// Where a unit's character id lives inside the unit's data:
//
//   [units]
//   character_id_field = 0x24   ; byte offset from the start of the unit
//   character_id_size = 2       ; 1, 2 or 4 bytes (default 2)
//
// Used when the game has no separate function for unit_character_id.
struct FieldLocation {
    bool set = false;
    uint32_t offset = 0;
    int size = 2;
};

// Read a little-endian field of `field.size` bytes at `base + field.offset`.
uint32_t readField(const void* base, const FieldLocation& field);

struct HookTargetsResult {
    std::vector<HookTarget> targets;
    FieldLocation characterIdField;
    std::vector<std::string> warnings;
};

HookTargetsResult parseHookTargets(std::string_view text);

struct ResolvedHook {
    bool ok = false;
    uint64_t offset = 0;  // relative to the main module start
    std::string error;
};

// Resolve a target against the main module's code. `text` is its .text
// section, which starts `textOffset` bytes into the module.
ResolvedHook resolveHook(const HookTarget& target, const uint8_t* text, size_t textSize, uint64_t textOffset);

}  // namespace coop
