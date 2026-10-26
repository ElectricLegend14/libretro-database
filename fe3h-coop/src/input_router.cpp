#include "coop/input_router.hpp"

namespace coop {

InputRouter::Decision InputRouter::route(uint32_t requested) const {
    bool primary = requested == kNpadNo1 || requested == kNpadHandheld;
    if (!primary) return {Route::Idle, requested};

    auto target = uint32_t(state_.player(state_.activePlayer()).controller);
    // Controller 1 is whatever the game already reads, including handheld mode.
    if (target == kNpadNo1) return {Route::Passthrough, requested};
    return {Route::Redirect, target};
}

uint64_t InputRouter::mask(uint64_t buttons) {
    if ((buttons & kPassHold) == kPassHold) buttons &= ~kPassPress;
    return buttons;
}

std::optional<TurnChange> InputRouter::observe(uint64_t buttons) {
    bool pressed = (buttons & kPassHold) == kPassHold && (buttons & kPassPress) && !(previous_ & kPassPress);
    previous_ = buttons;
    if (!pressed) return std::nullopt;

    int before = state_.activePlayer();
    state_.nextPlayer();
    TurnChange change;
    change.player = state_.activePlayer();
    change.passController = state_.player(before).controller == state_.player(change.player).controller;
    // The next player's pad may still have Plus held (shared controller):
    // require a fresh press before passing again.
    previous_ = kPassPress;
    return change;
}

}  // namespace coop
