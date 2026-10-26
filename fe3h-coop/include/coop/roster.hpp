#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coop {

// A playable character the co-op config can refer to by name.
//
// `gameId` is the character's index in the game's own character table. These
// still have to be confirmed by reverse engineering (see docs in README), so
// they start as -1. Players can always fall back to "#<id>" in the config.
struct Character {
    std::string name;
    int gameId = -1;
};

const std::vector<Character>& roster();

// Case-insensitive lookup by name, or "#<id>" for a raw game character id.
// Returns the canonical name ("Edelgard", "#42").
std::optional<std::string> resolveCharacter(std::string_view text);

// Name the config uses for a game character id: the roster name once its
// gameId is known, otherwise "#<id>".
std::string characterNameForGameId(int id);

}  // namespace coop
