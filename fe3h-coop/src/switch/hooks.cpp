// Game-side glue for the Switch build (exlaunch). Not compiled on desktop.
//
// None of the game functions below have been located yet. Each TODO names
// what to look for in Ghidra/IDA; once an offset is found, fill it in and
// replace the placeholder signature with the real one.

#ifdef __SWITCH__

#include <cstdio>
#include <memory>
#include <string>

#include "coop/coop_state.hpp"
#include "lib.hpp"  // exlaunch

namespace {

constexpr const char* kConfigPath = "sdmc:/config/fe3h-coop/coop.ini";

std::unique_ptr<coop::CoopState> g_state;

std::string readFile(const char* path) {
    std::string out;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

void saveIfDirty() {
    if (!g_state || !g_state->dirty()) return;
    if (FILE* f = std::fopen(kConfigPath, "wb")) {
        auto text = coop::serializeSettings(g_state->settings());
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        g_state->markSaved();
    }
}

// TODO: map the game's unit struct -> character name. The character index
// should sit near the top of the per-unit struct; pair it with roster ids.
std::string characterOf(void* unit);

// TODO: find the per-frame "can this unit be selected" check used when the
// player presses A on a unit during the player phase.
HOOK_DEFINE_TRAMPOLINE(CanSelectUnit) {
    static bool Callback(void* unit) {
        if (g_state && !g_state->canSelect(characterOf(unit))) return false;
        return Orig(unit);
    }
};

// TODO: find where the battle input handler reads the pad (npad) state, and
// drop input from controllers that aren't the active player's.
HOOK_DEFINE_TRAMPOLINE(ReadBattleInput) {
    static bool Callback(int controller, void* out) {
        if (g_state && !g_state->acceptsInput(controller)) return false;
        return Orig(controller, out);
    }
};

// TODO: battle prep unit list. When ZR is pressed on a highlighted unit,
// toggle the claim for the player holding that controller.
// TODO: player-phase start / "end turn". Advance g_state->nextPlayer() and
// show the banner in player(activePlayer()).color instead of ending the phase
// until every player has ended their part of the turn.
// TODO: cursor and unit outline draw calls. Tint with
// player(activePlayer()).color.toArgb() and outlineColorOf(character).

}  // namespace

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();

    auto result = coop::parseSettings(readFile(kConfigPath));
    // Warnings are non-fatal. On errors the parser already fell back to defaults.
    g_state = std::make_unique<coop::CoopState>(std::move(result.settings));

    // CanSelectUnit::InstallAtOffset(0x0 /* TODO */);
    // ReadBattleInput::InstallAtOffset(0x0 /* TODO */);
    (void)saveIfDirty;
}

#endif  // __SWITCH__
