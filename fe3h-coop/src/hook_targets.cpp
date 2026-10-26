#include "coop/hook_targets.hpp"

#include <charconv>
#include <cstring>

#include "coop/ini.hpp"

namespace coop {

const std::vector<KnownHook>& knownHooks() {
    static const std::vector<KnownHook> kHooks = {
        {"unit_character_id", "Returns a battle unit's character id (int f(Unit*))."},
        {"can_select_unit", "Decides whether pressing A on a unit picks it up in the player phase (bool f(Unit*))."},
        {"begin_player_phase", "Runs once when the \"Player Phase\" banner appears."},
        {"end_player_phase", "Runs when the player chooses End Turn, before the enemy phase starts."},
        {"prep_unit_list_input", "Handles button presses on the battle preparation unit list."},
        {"draw_map_cursor", "Draws the blue map cursor each frame."},
        {"draw_unit_outline", "Draws the colored ring under a unit on the map."},
    };
    return kHooks;
}

namespace {

bool isKnown(const std::string& name) {
    for (const auto& h : knownHooks())
        if (name == h.name) return true;
    return false;
}

bool parseHex(std::string_view s, uint64_t& out) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);
    if (s.empty()) return false;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out, 16);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace

HookTargetsResult parseHookTargets(std::string_view text) {
    HookTargetsResult result;
    IniFile ini = parseIni(text);
    result.warnings = std::move(ini.warnings);
    auto warn = [&](int line, const std::string& msg) {
        result.warnings.push_back("line " + std::to_string(line) + ": " + msg);
    };

    for (const auto& [section, values] : ini.sections) {
        if (section != "hooks" && section != "units") result.warnings.push_back("unknown section [" + section + "]");
    }

    // [units]: where the character id sits inside a unit, for when the game
    // has no separate function for it.
    auto& units = ini.sections["units"];
    for (const auto& [key, entry] : units) {
        if (key != "character_id_field" && key != "character_id_size") warn(entry.line, "unknown option '" + key + "'");
    }
    if (auto it = units.find("character_id_field"); it != units.end() && !it->second.value.empty()) {
        uint64_t field = 0;
        if (!parseHex(it->second.value, field) || field > 0xFFFF) {
            warn(it->second.line, "character_id_field: expected a hex offset like 0x24");
        } else {
            result.characterIdField = {true, uint32_t(field), 2};
            if (auto sz = units.find("character_id_size"); sz != units.end() && !sz->second.value.empty()) {
                const auto& v = sz->second.value;
                if (v == "1" || v == "2" || v == "4") {
                    result.characterIdField.size = v[0] - '0';
                } else {
                    warn(sz->second.line, "character_id_size must be 1, 2 or 4 (bytes)");
                }
            }
        }
    }

    for (const auto& [name, entry] : ini.sections["hooks"]) {
        const auto& [value, line] = entry;
        if (value.empty()) continue;  // not found yet
        if (!isKnown(name)) {
            warn(line, "unknown hook '" + name + "'");
            continue;
        }

        HookTarget t;
        t.name = name;
        t.line = line;
        std::string_view v = value;
        auto lower = toLower(v);
        if (lower.rfind("pattern:", 0) == 0 || lower.rfind("call:", 0) == 0) {
            bool call = lower[0] == 'c';
            t.kind = call ? HookTarget::Kind::Call : HookTarget::Kind::Pattern;
            auto parsed = parsePattern(v.substr(v.find(':') + 1));
            if (!parsed) {
                warn(line, name + ": pattern must be hex bytes like 'FD 7B ?? A9'");
                continue;
            }
            if (call && !requireLeadingBl(*parsed)) {
                warn(line, name + ": 'call:' pattern must start with the 4 bytes of a BL");
                continue;
            }
            t.pattern = std::move(*parsed);
        } else if (!parseHex(v, t.offset)) {
            warn(line, name + ": expected a hex offset (0x1234), 'pattern: ...' or 'call: ...'");
            continue;
        }
        result.targets.push_back(std::move(t));
    }
    return result;
}

uint32_t readField(const void* base, const FieldLocation& field) {
    const auto* p = static_cast<const uint8_t*>(base) + field.offset;
    uint32_t value = 0;
    for (int i = 0; i < field.size; ++i) value |= uint32_t(p[i]) << (8 * i);
    return value;
}

ResolvedHook resolveHook(const HookTarget& t, const uint8_t* text, size_t textSize, uint64_t textOffset) {
    ResolvedHook r;
    if (t.kind == HookTarget::Kind::Offset) {
        if (t.offset < textOffset || t.offset >= textOffset + textSize || t.offset % 4) {
            r.error = t.name + ": offset is not an instruction in the game's code";
            return r;
        }
        r.ok = true;
        r.offset = t.offset;
        return r;
    }

    auto scan = scanUnique(text, textSize, t.pattern);
    if (scan.status == ScanResult::NotFound) {
        r.error = t.name + ": pattern not found (different game version?)";
        return r;
    }
    if (scan.status == ScanResult::Ambiguous) {
        r.error = t.name + ": pattern matches more than once; make it longer";
        return r;
    }

    uint64_t at = textOffset + scan.offset;
    if (t.kind == HookTarget::Kind::Call) {
        uint32_t insn;
        std::memcpy(&insn, text + scan.offset, sizeof insn);
        auto target = branchTarget(insn, at);
        if (!target) {
            r.error = t.name + ": 'call:' pattern must start on a BL instruction";
            return r;
        }
        at = *target;
    }
    r.ok = true;
    r.offset = at;
    return r;
}

}  // namespace coop
