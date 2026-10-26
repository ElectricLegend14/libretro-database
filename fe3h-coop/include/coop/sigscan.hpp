#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace coop {

// A byte pattern such as "FD 7B BF A9 ?? ?? 0? 94". "??" matches any byte and
// "0?" / "?4" match one half of a byte. Patterns survive game updates much
// better than raw offsets do.
struct Pattern {
    std::vector<uint8_t> value;
    std::vector<uint8_t> mask;  // bits that must equal `value`; 0x00 = wildcard

    size_t size() const { return value.size(); }
};

std::optional<Pattern> parsePattern(std::string_view text);

// Make the first instruction match any AArch64 BL, whichever way it jumps
// (BL encodes its offset in the top byte too: 94-97). Fails on patterns
// shorter than one instruction.
bool requireLeadingBl(Pattern& pattern);

struct ScanResult {
    enum Status { Found, NotFound, Ambiguous } status = NotFound;
    size_t offset = 0;   // first match, relative to `data`
    size_t matches = 0;  // total matches (stops counting at 2)
};

// Scan for exactly one match. AArch64 code is 4-byte aligned, so by default
// only aligned positions are tried. An ambiguous pattern is an error: hooking
// the wrong one of two lookalike functions is worse than not hooking at all.
ScanResult scanUnique(const uint8_t* data, size_t size, const Pattern& pattern, size_t alignment = 4);

// If `insn` is an AArch64 B or BL at address `pc`, return where it jumps to.
std::optional<uint64_t> branchTarget(uint32_t insn, uint64_t pc);

}  // namespace coop
