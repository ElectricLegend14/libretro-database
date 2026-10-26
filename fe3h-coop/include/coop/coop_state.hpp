#pragma once

#include <optional>
#include <string>

#include "coop/settings.hpp"

namespace coop {

// Runtime view of the settings that the game hooks query every frame.
// Players are 1-based here to match the config file and the on-screen text.
class CoopState {
public:
    explicit CoopState(Settings settings) : settings_(std::move(settings)) {}

    const Settings& settings() const { return settings_; }
    int playerCount() const { return int(settings_.players.size()); }
    const PlayerSettings& player(int p) const { return settings_.players.at(p - 1); }

    // Which player owns this character, if anyone.
    std::optional<int> ownerOf(const std::string& character) const;

    // Color to draw this character's outline with (owner's color), if tinting is on.
    std::optional<Color> outlineColorOf(const std::string& character) const;

    // Whose turn it is within the player phase. The banner and cursor use this.
    int activePlayer() const { return active_; }
    void setActivePlayer(int p);
    void nextPlayer() { setActivePlayer(active_ % playerCount() + 1); }

    // Should input from `controller` be accepted right now? Hot-seat setups can
    // give several players the same controller; then whoever's turn it is plays.
    bool acceptsInput(int controller) const { return player(active_).controller == controller; }

    // May the active player pick up this unit on the map?
    bool canSelect(const std::string& character) const;

    // Battle-prep claiming. claim() steals from nobody: it fails if another
    // player already owns the unit. toggleClaim() is what the prep-screen
    // button calls: unclaimed -> mine, mine -> unclaimed.
    bool claim(int p, const std::string& character);
    bool release(int p, const std::string& character);
    bool toggleClaim(int p, const std::string& character);

    // True if claims changed since the last markSaved() and the config should be rewritten.
    bool dirty() const { return dirty_; }
    void markSaved() { dirty_ = false; }

private:
    Settings settings_;
    int active_ = 1;
    bool dirty_ = false;
};

}  // namespace coop
