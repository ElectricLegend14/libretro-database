#include "coop/settings.hpp"

#include <charconv>
#include <map>

#include "coop/ini.hpp"
#include "coop/roster.hpp"

namespace coop {

namespace {

bool parseInt(std::string_view s, int& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool parseBool(std::string_view s, bool& out) {
    auto v = toLower(s);
    if (v == "true" || v == "yes" || v == "on" || v == "1") return out = true, true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return out = false, true;
    return false;
}

std::vector<std::string_view> splitList(std::string_view s) {
    std::vector<std::string_view> items;
    while (!s.empty()) {
        auto comma = s.find(',');
        auto item = trim(s.substr(0, comma));
        if (!item.empty()) items.push_back(item);
        if (comma == std::string_view::npos) break;
        s.remove_prefix(comma + 1);
    }
    return items;
}

// "player2" -> 2, anything else -> 0
int playerSection(std::string_view section) {
    constexpr std::string_view prefix = "player";
    if (section.substr(0, prefix.size()) != prefix) return 0;
    int n = 0;
    return parseInt(section.substr(prefix.size()), n) ? n : 0;
}

}  // namespace

Settings defaultSettings() {
    Settings s;
    for (int p = 1; p <= kMinPlayers; ++p) {
        s.players.push_back({"Player " + std::to_string(p), defaultColorFor(p), p - 1, {}});
    }
    return s;
}

LoadResult parseSettings(std::string_view text) {
    LoadResult result;
    auto warn = [&](int line, const std::string& msg) {
        result.warnings.push_back("line " + std::to_string(line) + ": " + msg);
    };

    IniFile ini = parseIni(text);
    result.warnings = std::move(ini.warnings);
    auto& sections = ini.sections;

    // [coop] general options.
    Settings& s = result.settings;
    int playerCount = kMinPlayers;
    for (const auto& [key, entry] : sections["coop"]) {
        const auto& [value, line] = entry;
        if (key == "players") {
            if (!parseInt(value, playerCount) || playerCount < kMinPlayers || playerCount > kMaxPlayers) {
                result.errors.push_back("line " + std::to_string(line) + ": players must be " +
                                        std::to_string(kMinPlayers) + "-" + std::to_string(kMaxPlayers));
            }
        } else if (key == "unclaimed_units") {
            auto v = toLower(value);
            if (v == "shared") s.unclaimed = UnclaimedPolicy::Shared;
            else if (v == "host") s.unclaimed = UnclaimedPolicy::Host;
            else if (v == "locked") s.unclaimed = UnclaimedPolicy::Locked;
            else warn(line, "unclaimed_units must be shared, host or locked");
        } else if (key == "in_game_claiming") {
            if (!parseBool(value, s.allowInGameClaiming)) warn(line, "in_game_claiming must be true/false");
        } else if (key == "tint_unit_outlines") {
            if (!parseBool(value, s.tintUnitOutlines)) warn(line, "tint_unit_outlines must be true/false");
        } else {
            warn(line, "unknown option '" + key + "'");
        }
    }
    if (!result.ok()) {
        result.settings = defaultSettings();
        return result;
    }

    // [playerN] sections.
    std::map<std::string, int> owner;  // character -> 1-based player who claimed it first
    for (int p = 1; p <= playerCount; ++p) {
        PlayerSettings player{"Player " + std::to_string(p), defaultColorFor(p), p - 1, {}};
        for (const auto& [key, entry] : sections["player" + std::to_string(p)]) {
            const auto& [value, line] = entry;
            if (key == "name") {
                if (!value.empty()) player.name = value;
            } else if (key == "color") {
                if (auto c = parseColor(value)) player.color = *c;
                else warn(line, "unknown color '" + value + "', using " + player.color.toHex());
            } else if (key == "controller") {
                int n = 0;
                if (parseInt(value, n) && n >= 1 && n <= 8) player.controller = n - 1;
                else warn(line, "controller must be 1-8");
            } else if (key == "characters") {
                for (auto item : splitList(value)) {
                    auto name = resolveCharacter(item);
                    if (!name) {
                        warn(line, "unknown character '" + std::string(item) + "'");
                        continue;
                    }
                    auto [it, inserted] = owner.emplace(*name, p);
                    if (!inserted) {
                        if (it->second != p) {
                            warn(line, *name + " is already claimed by player " + std::to_string(it->second));
                        }
                        continue;
                    }
                    player.characters.push_back(*name);
                }
            } else {
                warn(line, "unknown option '" + key + "'");
            }
        }
        s.players.push_back(std::move(player));
    }

    for (const auto& [name, section] : sections) {
        int p = playerSection(name);
        if (p > playerCount) {
            result.warnings.push_back("[" + name + "] ignored: players = " + std::to_string(playerCount));
        } else if (p == 0 && name != "coop") {
            result.warnings.push_back("unknown section [" + name + "]");
        }
    }

    for (size_t a = 0; a < s.players.size(); ++a) {
        for (size_t b = a + 1; b < s.players.size(); ++b) {
            if (colorDistance(s.players[a].color, s.players[b].color) < kMinDistinctColorDistance) {
                result.warnings.push_back(s.players[a].name + " and " + s.players[b].name +
                                          " have very similar colors");
            }
        }
    }

    return result;
}

std::string serializeSettings(const Settings& s) {
    const char* policy = s.unclaimed == UnclaimedPolicy::Host     ? "host"
                         : s.unclaimed == UnclaimedPolicy::Locked ? "locked"
                                                                  : "shared";
    auto boolText = [](bool b) { return b ? "true" : "false"; };

    std::string out;
    out += "[coop]\n";
    out += "players = " + std::to_string(s.players.size()) + "\n";
    out += std::string("unclaimed_units = ") + policy + "\n";
    out += std::string("in_game_claiming = ") + boolText(s.allowInGameClaiming) + "\n";
    out += std::string("tint_unit_outlines = ") + boolText(s.tintUnitOutlines) + "\n";

    for (size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        out += "\n[player" + std::to_string(i + 1) + "]\n";
        out += "name = " + p.name + "\n";
        out += "color = " + p.color.toHex() + "\n";
        out += "controller = " + std::to_string(p.controller + 1) + "\n";
        out += "characters = ";
        for (size_t c = 0; c < p.characters.size(); ++c) out += (c ? ", " : "") + p.characters[c];
        out += "\n";
    }
    return out;
}

}  // namespace coop
