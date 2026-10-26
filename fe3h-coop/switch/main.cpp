// FE3H co-op: the exlaunch module that runs inside the game.
//
// What works without any reverse engineering:
//   Controller relay. The game only listens to controller 1 / handheld mode.
//   The nn::hid hooks below answer those reads with the active player's
//   controller, make every other controller look idle, and pass the turn on
//   ZL + ZR + Plus.
//
// What switches on once you fill in offsets.ini (see docs/REVERSING.md):
//   can_select_unit + unit_character_id  ->  players can only pick up their own units
//   begin_player_phase + end_player_phase  ->  End Turn hands over to the next player
//
// Game-function callbacks take three extra u64 arguments and pass them on
// to the original, so x1-x3 survive even if a function takes more arguments
// than we know about. Confirm each signature in Ghidra before relying on it.

#include <string>
#include <type_traits>
#include <vector>

#include "coop/coop_state.hpp"
#include "coop/hook_targets.hpp"
#include "coop/input_router.hpp"
#include "coop/roster.hpp"
#include "coop/settings.hpp"
#include "lib.hpp"
#include "nn/fs.hpp"

// nn::hid declarations. These link against the game's copy of the SDK. If the
// module fails to load with a missing symbol, compare these mangled names
// with the imports listed in Ghidra (docs/REVERSING.md, step 2).
namespace nn::hid {

using NpadIdType = u32;

struct AnalogStickState {
    s32 x, y;
};

struct NpadBaseState {
    s64 samplingNumber;
    u64 buttons;
    AnalogStickState analogStickL;
    AnalogStickState analogStickR;
    u32 attributes;  // bit 0: connected
    u32 reserved;
};
static_assert(sizeof(NpadBaseState) == 0x28);

struct NpadFullKeyState : NpadBaseState {};
struct NpadHandheldState : NpadBaseState {};
struct NpadJoyDualState : NpadBaseState {};

void GetNpadState(NpadFullKeyState*, const NpadIdType&);
void GetNpadState(NpadHandheldState*, const NpadIdType&);
void GetNpadState(NpadJoyDualState*, const NpadIdType&);
int GetNpadStates(NpadFullKeyState*, int, const NpadIdType&);
int GetNpadStates(NpadHandheldState*, int, const NpadIdType&);
int GetNpadStates(NpadJoyDualState*, int, const NpadIdType&);

}  // namespace nn::hid

namespace {

constexpr const char* kMount = "coopsd";
constexpr const char* kConfigPath = "coopsd:/config/fe3h-coop/coop.ini";
constexpr const char* kOffsetsPath = "coopsd:/config/fe3h-coop/offsets.ini";

coop::CoopState* g_state = nullptr;
coop::InputRouter* g_router = nullptr;

void lazyInit();

void announce(const coop::TurnChange& change) {
    if (change.phaseOver) {
        Logging.Log("[coop] everyone has moved: enemy phase");
        return;
    }
    const auto& p = g_state->player(change.player);
    Logging.Log("[coop] %s's turn (%s)%s", p.name.c_str(), p.color.toHex().c_str(),
                change.passController ? " - pass them the controller" : "");
}

bool readSdFile(const char* path, std::string& out) {
    nn::fs::FileHandle file;
    if (nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read) != 0) return false;
    long size = 0;
    bool ok = nn::fs::GetFileSize(&size, file) == 0 && size >= 0 && size < 64 * 1024;
    if (ok) {
        out.resize(size_t(size));
        ok = nn::fs::ReadFile(file, 0, out.data(), ulong(size)) == 0;
    }
    nn::fs::CloseFile(file);
    return ok;
}

// ---- Controller relay -------------------------------------------------------

void applyActive(nn::hid::NpadBaseState* states, int count) {
    if (count <= 0) return;
    // Disconnected styles read as all zeros; only real input may trigger a pass.
    if (states[0].attributes & 1) {
        if (auto change = g_router->observe(states[0].buttons)) announce(*change);
    }
    for (int i = 0; i < count; ++i) states[i].buttons = coop::InputRouter::mask(states[i].buttons);
}

void makeIdle(nn::hid::NpadBaseState* states, int count) {
    for (int i = 0; i < count; ++i) {
        states[i].buttons = 0;
        states[i].analogStickL = {0, 0};
        states[i].analogStickR = {0, 0};
    }
}

template <typename Hook, typename State>
int relay(State* out, int count, const nn::hid::NpadIdType& id);

HOOK_DEFINE_TRAMPOLINE(FullKeyStates) {
    static int Callback(nn::hid::NpadFullKeyState* out, int count, const nn::hid::NpadIdType& id) {
        return relay<FullKeyStates>(out, count, id);
    }
};
HOOK_DEFINE_TRAMPOLINE(HandheldStates) {
    static int Callback(nn::hid::NpadHandheldState* out, int count, const nn::hid::NpadIdType& id) {
        return relay<HandheldStates>(out, count, id);
    }
};
HOOK_DEFINE_TRAMPOLINE(JoyDualStates) {
    static int Callback(nn::hid::NpadJoyDualState* out, int count, const nn::hid::NpadIdType& id) {
        return relay<JoyDualStates>(out, count, id);
    }
};

template <typename Hook, typename State>
int relay(State* out, int count, const nn::hid::NpadIdType& id) {
    if (!g_router) lazyInit();

    auto d = g_router->route(id);
    if (d.route == coop::InputRouter::Route::Passthrough) {
        int n = Hook::Orig(out, count, id);
        applyActive(out, n);
        return n;
    }
    if (d.route == coop::InputRouter::Route::Idle) {
        int n = Hook::Orig(out, count, id);
        makeIdle(out, n);
        return n;
    }

    // Redirect: read the active player's controller in the style the game
    // asked for. A Pro Controller reports as FullKey and a pair of Joy-Con
    // as JoyDual, so if nothing's there, try those styles too. All styles
    // share one layout, so the game's buffer can take either.
    const nn::hid::NpadIdType target = d.npadId;
    auto* base = static_cast<nn::hid::NpadBaseState*>(out);
    auto connected = [&](int got) { return got > 0 && (base[0].attributes & 1); };

    int n = Hook::Orig(out, count, target);
    if (!connected(n) && !std::is_same_v<State, nn::hid::NpadFullKeyState>) {
        int got = FullKeyStates::Orig(static_cast<nn::hid::NpadFullKeyState*>(base), count, target);
        if (got > 0) n = got;
    }
    if (!connected(n) && !std::is_same_v<State, nn::hid::NpadJoyDualState>) {
        int got = JoyDualStates::Orig(static_cast<nn::hid::NpadJoyDualState*>(base), count, target);
        if (got > 0) n = got;
    }
    applyActive(out, n);
    return n;
}

// The single-state variants go through the same relay with count = 1.
template <typename Hook, typename State>
void relayOne(State* out, const nn::hid::NpadIdType& id) {
    relay<Hook>(out, 1, id);
}

HOOK_DEFINE_TRAMPOLINE(FullKeyState) {
    static void Callback(nn::hid::NpadFullKeyState* out, const nn::hid::NpadIdType& id) {
        relayOne<FullKeyStates>(out, id);
    }
};
HOOK_DEFINE_TRAMPOLINE(HandheldState) {
    static void Callback(nn::hid::NpadHandheldState* out, const nn::hid::NpadIdType& id) {
        relayOne<HandheldStates>(out, id);
    }
};
HOOK_DEFINE_TRAMPOLINE(JoyDualState) {
    static void Callback(nn::hid::NpadJoyDualState* out, const nn::hid::NpadIdType& id) {
        relayOne<JoyDualStates>(out, id);
    }
};

// ---- Game functions (from offsets.ini) -------------------------------------

using CharacterIdFn = int (*)(void* unit, u64, u64, u64);
CharacterIdFn g_unitCharacterId = nullptr;

std::string characterOf(void* unit) {
    return coop::characterNameForGameId(g_unitCharacterId(unit, 0, 0, 0));
}

// Log each unmapped character id once, so pressing A on every unit tells you
// which id is which (fill them into src/roster.cpp).
void reportUnmapped(const std::string& name) {
    static std::vector<std::string> seen;
    if (name.empty() || name[0] != '#') return;
    for (const auto& s : seen)
        if (s == name) return;
    seen.push_back(name);
    Logging.Log("[coop] unit with character id %s selected (not in the roster yet)", name.c_str() + 1);
}

HOOK_DEFINE_TRAMPOLINE(CanSelectUnit) {
    static bool Callback(void* unit, u64 a1, u64 a2, u64 a3) {
        if (g_unitCharacterId) {
            auto name = characterOf(unit);
            reportUnmapped(name);
            if (!g_state->canSelect(name)) return false;
        }
        return Orig(unit, a1, a2, a3);
    }
};

HOOK_DEFINE_TRAMPOLINE(BeginPlayerPhase) {
    static void Callback(void* self, u64 a1, u64 a2, u64 a3) {
        Orig(self, a1, a2, a3);
        // Until the deployed-unit list is found, assume every claimed unit is on
        // the map, plus one unclaimed placeholder so shared/host rules apply.
        std::vector<std::string> deployed = {"#unclaimed"};
        for (int p = 1; p <= g_state->playerCount(); ++p)
            for (const auto& c : g_state->player(p).characters) deployed.push_back(c);
        g_state->beginPlayerPhase(deployed);
        announce({false, g_state->activePlayer(), false});
    }
};

HOOK_DEFINE_TRAMPOLINE(EndPlayerPhase) {
    static void Callback(void* self, u64 a1, u64 a2, u64 a3) {
        auto change = g_state->endTurn();
        announce(change);
        if (change.phaseOver) Orig(self, a1, a2, a3);
        // Otherwise swallow End Turn: the next player moves their units.
    }
};

void installGameHooks(const std::string& text) {
    auto parsed = coop::parseHookTargets(text);
    for (const auto& w : parsed.warnings) Logging.Log("[coop] offsets.ini %s", w.c_str());

    const auto& mod = exl::util::GetMainModuleInfo();
    const auto* code = reinterpret_cast<const uint8_t*>(mod.m_Text.m_Start);
    const uint64_t textOffset = mod.m_Text.m_Start - mod.m_Total.m_Start;

    for (const auto& t : parsed.targets) {
        auto r = coop::resolveHook(t, code, mod.m_Text.m_Size, textOffset);
        if (!r.ok) {
            Logging.Log("[coop] %s", r.error.c_str());
            continue;
        }
        Logging.Log("[coop] hook %s at main+0x%lx", t.name.c_str(), (unsigned long)r.offset);
        if (t.name == "unit_character_id") {
            g_unitCharacterId = reinterpret_cast<CharacterIdFn>(exl::util::modules::GetTargetOffset(r.offset));
        } else if (t.name == "can_select_unit") {
            CanSelectUnit::InstallAtOffset(ptrdiff_t(r.offset));
        } else if (t.name == "begin_player_phase") {
            BeginPlayerPhase::InstallAtOffset(ptrdiff_t(r.offset));
        } else if (t.name == "end_player_phase") {
            EndPlayerPhase::InstallAtOffset(ptrdiff_t(r.offset));
        } else {
            Logging.Log("[coop] %s is found but its hook isn't written yet", t.name.c_str());
        }
    }
}

// ---- Startup ----------------------------------------------------------------

void loadConfig() {
    std::string text;
    coop::Settings settings = coop::defaultSettings();

    if (nn::fs::MountSdCardForDebug(kMount) != 0) {
        Logging.Log("[coop] can't open the SD card; using default settings");
    } else if (!readSdFile(kConfigPath, text)) {
        Logging.Log("[coop] no %s; using default settings", kConfigPath);
    } else {
        auto result = coop::parseSettings(text);
        for (const auto& e : result.errors) Logging.Log("[coop] coop.ini error %s", e.c_str());
        for (const auto& w : result.warnings) Logging.Log("[coop] coop.ini %s", w.c_str());
        settings = std::move(result.settings);
    }

    g_state = new coop::CoopState(std::move(settings));
    Logging.Log("[coop] %d players ready. Hold ZL + ZR and press Plus to pass the turn.", g_state->playerCount());

    if (readSdFile(kOffsetsPath, text)) installGameHooks(text);
}

// The SD card isn't usable yet when exl_main runs, so settings load on the
// game's first controller read instead.
void lazyInit() {
    loadConfig();
    g_router = new coop::InputRouter(*g_state);
}

}  // namespace

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();

    using namespace nn::hid;
    FullKeyStates::InstallAtFuncPtr(static_cast<int (*)(NpadFullKeyState*, int, const NpadIdType&)>(GetNpadStates));
    HandheldStates::InstallAtFuncPtr(static_cast<int (*)(NpadHandheldState*, int, const NpadIdType&)>(GetNpadStates));
    JoyDualStates::InstallAtFuncPtr(static_cast<int (*)(NpadJoyDualState*, int, const NpadIdType&)>(GetNpadStates));
    FullKeyState::InstallAtFuncPtr(static_cast<void (*)(NpadFullKeyState*, const NpadIdType&)>(GetNpadState));
    HandheldState::InstallAtFuncPtr(static_cast<void (*)(NpadHandheldState*, const NpadIdType&)>(GetNpadState));
    JoyDualState::InstallAtFuncPtr(static_cast<void (*)(NpadJoyDualState*, const NpadIdType&)>(GetNpadState));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("fe3h-coop: unexpected exception");
}
