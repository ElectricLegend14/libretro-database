#include "coop/settings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <sstream>

#include "coop/roster.hpp"

namespace coop {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool parseInt(std::string_view s, int& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool parseBool(std::string_view s, bool& out) {
    auto v = lower(s);
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

using Section = std::map<std::string, std::pair<std::string, int>>;  // key -> (value, line)

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

    // Pass 1: split into sections.
    std::map<std::string, Section> sections;
    std::string current;
    int lineNo = 0;
    while (!text.empty()) {
        ++lineNo;
        auto nl = text.find('\n');
        auto line = trim(text.substr(0, nl));
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            if (line.back() != ']') {
                warn(lineNo, "malformed section header");
                continue;
            }
            current = lower(trim(line.substr(1, line.size() - 2)));
            sections[current];
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn(lineNo, "expected 'key = value'");
            continue;
        }
        if (current.empty()) {
            warn(lineNo, "option outside of any [section]");
            continue;
        }
        auto value = trim(line.substr(eq + 1));
        // Inline comments need whitespace before ';' so colors like #FF0000 survive.
        if (auto c = value.find(" ;"); c != std::string_view::npos) value = trim(value.substr(0, c));
        sections[current][lower(trim(line.substr(0, eq)))] = {std::string(value), lineNo};
    }

    // Pass 2: [coop] general options.
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
            auto v = lower(value);
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

    // Pass 3: [playerN] sections.
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
        } else if (p == 0 && name != "coop" && !name.empty()) {
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
    std::ostringstream out;
    const char* policy = s.unclaimed == UnclaimedPolicy::Host     ? "host"
                         : s.unclaimed == UnclaimedPolicy::Locked ? "locked"
                                                                  : "shared";
    out << "[coop]\n"
        << "players = " << s.players.size() << "\n"
        << "unclaimed_units = " << policy << "\n"
        << "in_game_claiming = " << (s.allowInGameClaiming ? "true" : "false") << "\n"
        << "tint_unit_outlines = " << (s.tintUnitOutlines ? "true" : "false") << "\n";

    for (size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        out << "\n[player" << i + 1 << "]\n"
            << "name = " << p.name << "\n"
            << "color = " << p.color.toHex() << "\n"
            << "controller = " << p.controller + 1 << "\n"
            << "characters = ";
        for (size_t c = 0; c < p.characters.size(); ++c) out << (c ? ", " : "") << p.characters[c];
        out << "\n";
    }
    return out.str();
}

}  // namespace coop
