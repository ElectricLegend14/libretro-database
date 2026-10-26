#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace coop {

// A byte pattern in IDA style: "48 8B 05 ?? ?? ?? ?? 48 85 C0". `?` or `??`
// matches any byte. Signatures survive game updates where raw offsets don't.
struct Pattern {
  std::vector<int16_t> bytes;  // -1 = wildcard
};

std::optional<Pattern> parsePattern(std::string_view text);

std::vector<size_t> findAll(const uint8_t* data, size_t len, const Pattern& pattern);

// The single match, or nullopt if there are none or several. A signature
// that matches twice is ambiguous and must be made longer.
std::optional<size_t> findUnique(const uint8_t* data, size_t len, const Pattern& pattern);

// Follows an x64 RIP-relative operand. `at` is the instruction's offset,
// `dispOffset` where its 32-bit displacement sits inside it, `length` its size.
// Returns the offset the instruction points to, or nullopt if out of range.
std::optional<size_t> resolveRipRelative(const uint8_t* data, size_t len, size_t at, size_t dispOffset,
                                         size_t length);

}  // namespace coop
