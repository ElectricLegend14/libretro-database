#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "coop/color.hpp"

namespace coop {

constexpr int kMinPlayers = 2;
constexpr int kMaxPlayers = 4;

// Who moves units that no player has claimed.
enum class UnclaimedPolicy {
    Shared,   // any player may move them
    Host,     // only player 1 may move them
    Locked,   // nobody may move them until someone claims them
};

struct PlayerSettings {
    std::string name;                    // shown on the turn banner
    Color color;                         // cursor / outline / banner tint
    int controller = 0;                  // 0-based controller index
    std::vector<std::string> characters; // canonical names from the roster
};

struct Settings {
    std::vector<PlayerSettings> players;
    UnclaimedPolicy unclaimed = UnclaimedPolicy::Shared;
    bool allowInGameClaiming = true;     // claim/release units on the battle prep screen
    bool tintUnitOutlines = true;        // draw each unit's outline in its owner's color
};

struct LoadResult {
    Settings settings;
    std::vector<std::string> warnings;   // config still usable
    std::vector<std::string> errors;     // config rejected; `settings` holds defaults
    bool ok() const { return errors.empty(); }
};

// Two players, default colors, nothing claimed.
Settings defaultSettings();

// Parse the INI-style config (see config/coop.ini for the format).
LoadResult parseSettings(std::string_view text);

// Write settings back out, e.g. after players claimed units in-game.
std::string serializeSettings(const Settings& settings);

}  // namespace coop
