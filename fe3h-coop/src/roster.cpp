#include "coop/roster.hpp"

#include <cctype>
#include <charconv>

namespace coop {

const std::vector<Character>& roster() {
    static const std::vector<Character> kRoster = {
        {"Byleth"},
        // Black Eagles
        {"Edelgard"}, {"Hubert"}, {"Ferdinand"}, {"Linhardt"}, {"Caspar"},
        {"Bernadetta"}, {"Dorothea"}, {"Petra"},
        // Blue Lions
        {"Dimitri"}, {"Dedue"}, {"Felix"}, {"Ashe"}, {"Sylvain"},
        {"Mercedes"}, {"Annette"}, {"Ingrid"},
        // Golden Deer
        {"Claude"}, {"Lorenz"}, {"Raphael"}, {"Ignatz"}, {"Lysithea"},
        {"Marianne"}, {"Hilda"}, {"Leonie"},
        // Church of Seiros and staff
        {"Seteth"}, {"Flayn"}, {"Hanneman"}, {"Manuela"}, {"Gilbert"},
        {"Alois"}, {"Catherine"}, {"Shamir"}, {"Cyril"}, {"Jeritza"},
        {"Rhea"}, {"Anna"},
        // Ashen Wolves (Cindered Shadows DLC)
        {"Yuri"}, {"Balthus"}, {"Constance"}, {"Hapi"},
    };
    return kRoster;
}

std::optional<std::string> resolveCharacter(std::string_view text) {
    if (text.size() > 1 && text.front() == '#') {
        int id = 0;
        auto digits = text.substr(1);
        auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
        if (ec != std::errc{} || ptr != digits.data() + digits.size() || id < 0) return std::nullopt;
        return "#" + std::to_string(id);
    }

    for (const auto& c : roster()) {
        if (c.name.size() != text.size()) continue;
        bool match = true;
        for (size_t i = 0; i < text.size() && match; ++i) {
            match = std::tolower(static_cast<unsigned char>(c.name[i])) ==
                    std::tolower(static_cast<unsigned char>(text[i]));
        }
        if (match) return c.name;
    }
    return std::nullopt;
}

std::string characterNameForGameId(int id) {
    for (const auto& c : roster())
        if (c.gameId >= 0 && c.gameId == id) return c.name;
    return "#" + std::to_string(id);
}

}  // namespace coop
