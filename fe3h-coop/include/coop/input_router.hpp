#pragma once

#include <cstdint>
#include <optional>

#include "coop/coop_state.hpp"

namespace coop {

// nn::hid button bits (same order as libnx HidNpadButton).
namespace button {
constexpr uint64_t A = 1ull << 0, B = 1ull << 1, X = 1ull << 2, Y = 1ull << 3;
constexpr uint64_t L = 1ull << 6, R = 1ull << 7, ZL = 1ull << 8, ZR = 1ull << 9;
constexpr uint64_t Plus = 1ull << 10, Minus = 1ull << 11;
}  // namespace button

// nn::hid NpadIdType values: No1..No8 are 0..7, handheld mode is 0x20.
constexpr uint32_t kNpadNo1 = 0;
constexpr uint32_t kNpadHandheld = 0x20;

// Hold ZL + ZR and press Plus to hand the turn to the next player.
constexpr uint64_t kPassHold = button::ZL | button::ZR;
constexpr uint64_t kPassPress = button::Plus;

// The game is single-player and only reads controller 1 (or handheld mode).
// The router answers those reads with whichever controller belongs to the
// active player, and makes every other controller look idle.
class InputRouter {
public:
    enum class Route {
        Passthrough,  // read the controller the game asked for
        Redirect,     // read `npadId` instead
        Idle,         // report no buttons and centered sticks
    };
    struct Decision {
        Route route;
        uint32_t npadId;
    };

    explicit InputRouter(CoopState& state) : state_(state) {}

    Decision route(uint32_t requestedNpadId) const;

    // Buttons to hand to the game for the active pad: Plus is hidden while
    // ZL + ZR are held so the pass-turn combo never opens the game's menu.
    static uint64_t mask(uint64_t buttons);

    // Feed the newest button state read for the active player. Returns the
    // turn change when the pass-turn combo was just pressed.
    std::optional<TurnChange> observe(uint64_t buttons);

private:
    CoopState& state_;
    uint64_t previous_ = 0;
};

}  // namespace coop
