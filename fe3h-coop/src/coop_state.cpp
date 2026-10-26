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

bool CoopState::canSelect(const std::string& character) const {
    if (auto owner = ownerOf(character)) return *owner == active_;
    switch (settings_.unclaimed) {
        case UnclaimedPolicy::Shared: return true;
        case UnclaimedPolicy::Host: return active_ == 1;
        case UnclaimedPolicy::Locked: return false;
    }
    return false;
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
