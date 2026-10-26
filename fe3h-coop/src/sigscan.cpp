#include "coop/sigscan.hpp"

#include <cctype>

namespace coop {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = char(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// One hex digit or '?', as (value, mask) for a 4-bit half.
bool parseNibble(char c, uint8_t& value, uint8_t& mask) {
    if (c == '?') return value = 0, mask = 0, true;
    int v = hexValue(c);
    if (v < 0) return false;
    value = uint8_t(v);
    mask = 0xF;
    return true;
}

}  // namespace

std::optional<Pattern> parsePattern(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        auto token = text.substr(i, end - i);
        i = end;

        if (token == "?") {
            p.value.push_back(0);
            p.mask.push_back(0);
            continue;
        }
        uint8_t hv, hm, lv, lm;
        if (token.size() != 2 || !parseNibble(token[0], hv, hm) || !parseNibble(token[1], lv, lm)) {
            return std::nullopt;
        }
        p.value.push_back(uint8_t(hv << 4 | lv));
        p.mask.push_back(uint8_t(hm << 4 | lm));
    }
    // A pattern of only wildcards would match everywhere.
    bool anyFixed = false;
    for (auto m : p.mask) anyFixed |= m != 0;
    if (!anyFixed) return std::nullopt;
    return p;
}

bool requireLeadingBl(Pattern& p) {
    if (p.size() < 4) return false;
    // Little-endian: the opcode bits 100101 are the top 6 bits of byte 3.
    p.value[3] = uint8_t((p.value[3] & ~0xFC) | 0x94);
    p.mask[3] = uint8_t(p.mask[3] | 0xFC);
    return true;
}

ScanResult scanUnique(const uint8_t* data, size_t size, const Pattern& pattern, size_t alignment) {
    ScanResult result;
    const size_t n = pattern.size();
    if (n == 0 || n > size || alignment == 0) return result;

    for (size_t pos = 0; pos + n <= size; pos += alignment) {
        size_t k = 0;
        while (k < n && (data[pos + k] & pattern.mask[k]) == (pattern.value[k] & pattern.mask[k])) ++k;
        if (k != n) continue;

        if (result.matches++ == 0) result.offset = pos;
        if (result.matches > 1) {
            result.status = ScanResult::Ambiguous;
            return result;
        }
    }
    result.status = result.matches ? ScanResult::Found : ScanResult::NotFound;
    return result;
}

std::optional<uint64_t> branchTarget(uint32_t insn, uint64_t pc) {
    // B: 000101 imm26, BL: 100101 imm26
    uint32_t op = insn & 0xFC000000u;
    if (op != 0x14000000u && op != 0x94000000u) return std::nullopt;
    int64_t imm = insn & 0x03FFFFFFu;
    if (imm & 0x02000000) imm -= 0x04000000;  // sign-extend 26 bits
    return pc + uint64_t(imm * 4);
}

}  // namespace coop
