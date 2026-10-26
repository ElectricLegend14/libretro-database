#include "coop/coop_state.hpp"

#include <algorithm>

namespace coop {

std::optional<int> CoopState::ownerOf(const std::string& character) const {
    for (int p = 1; p <= playerCount(); ++p) {
        const auto& chars = player(p).characters;
        if (std::find(chars.begin(), chars.end(), character) != chars.end()) return p;
    }
    return std::nullopt;
}

std::optional<Color> CoopState::outlineColorOf(const std::string& character) const {
    if (!settings_.tintUnitOutlines) return std::nullopt;
    if (auto p = ownerOf(character)) return player(*p).color;
    return std::nullopt;
}

void CoopState::setActivePlayer(int p) {
    if (p >= 1 && p <= playerCount()) active_ = p;
}

bool CoopState::canSelect(int p, const std::string& character) const {
    if (auto owner = ownerOf(character)) return *owner == p;
    switch (settings_.unclaimed) {
        case UnclaimedPolicy::Shared: return true;
        case UnclaimedPolicy::Host: return p == 1;
        case UnclaimedPolicy::Locked: return false;
    }
    return false;
}

void CoopState::beginPlayerPhase(const std::vector<std::string>& deployed) {
    order_.clear();
    for (int p = 1; p <= playerCount(); ++p) {
        bool hasUnit = std::any_of(deployed.begin(), deployed.end(),
                                   [&](const std::string& c) { return canSelect(p, c); });
        if (hasUnit) order_.push_back(p);
    }
    // Nobody can move anything (e.g. everything locked): give player 1 the
    // turn so the game never stalls waiting for input.
    if (order_.empty()) order_.push_back(1);
    turnIndex_ = 0;
    active_ = order_.front();
}

TurnChange CoopState::endTurn() {
    TurnChange change;
    if (order_.empty() || turnIndex_ + 1 >= order_.size()) {
        change.phaseOver = true;
        return change;
    }
    int previous = active_;
    active_ = order_[++turnIndex_];
    change.player = active_;
    change.passController = player(previous).controller == player(active_).controller;
    return change;
}

bool CoopState::claim(int p, const std::string& character) {
    if (p < 1 || p > playerCount()) return false;
    if (ownerOf(character)) return false;
    settings_.players[p - 1].characters.push_back(character);
    dirty_ = true;
    return true;
}

bool CoopState::release(int p, const std::string& character) {
    if (p < 1 || p > playerCount()) return false;
    auto& chars = settings_.players[p - 1].characters;
    auto it = std::find(chars.begin(), chars.end(), character);
    if (it == chars.end()) return false;
    chars.erase(it);
    dirty_ = true;
    return true;
}

bool CoopState::toggleClaim(int p, const std::string& character) {
    if (!settings_.allowInGameClaiming) return false;
    return release(p, character) || claim(p, character);
}

}  // namespace coop
