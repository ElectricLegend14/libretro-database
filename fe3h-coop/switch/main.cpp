// FE3H co-op: the exlaunch module that runs inside the game.
//
// What works without any reverse engineering:
//   Controller relay. The game only listens to controller 1 / handheld mode.
//   The nn::hid hooks below answer those reads with the active player's
//   controller, make every other controller look idle, and pass the turn on
//   ZL + ZR + Plus.
//
// What switches on once you fill in offsets.ini (see docs/REVERSING.md):
//   can_select_unit + unit_character_id (or [units] character_id_field)
//                                        ->  players can only pick up their own units
//   begin_player_phase + end_player_phase  ->  End Turn hands over to the next player
//
// Every SDK function is looked up by name at runtime instead of being
// linked, so a name this game doesn't have is logged and skipped rather
// than crashing the game at boot.
//
// Game-function callbacks take three extra u64 arguments and pass them on
// to the original, so x1-x3 survive even if a function takes more arguments
// than we know about. Confirm each signature in Ghidra before relying on it.
//
// Build with -DFE3H_COOP_BARE for a test module that only logs that it
// loaded (switch/build.sh: COOP_BARE=1).

#include <string>
#include <type_traits>
#include <vector>

#include "coop/coop_state.hpp"
#include "coop/hook_targets.hpp"
#include "coop/input_router.hpp"
#include "coop/roster.hpp"
#include "coop/settings.hpp"
#include "lib.hpp"
#include "nn/fs/fs_types.hpp"

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

}  // namespace nn::hid

namespace {

// ---- SDK lookup -------------------------------------------------------------

// Address of a function the game's SDK exports, or 0 if it has none by that name.
uintptr_t findSdkFunction(const char* name) {
    for (auto index : {exl::util::ModuleIndex::Sdk, exl::util::ModuleIndex::Main}) {
        if (!exl::util::HasModule(index)) continue;
        const auto* sym = exl::reloc::GetSymbol(index, name);
        if (sym && sym->st_value) return exl::util::GetModuleInfo(index).m_Total.m_Start + sym->st_value;
    }
    return 0;
}

// The first of several spellings that exists. NpadIdType is a plain u32 in
// the SDK headers we know of (RKj); the enum spelling is a fallback.
uintptr_t findSdkFunction(std::initializer_list<const char*> names, const char*& found) {
    for (const char* n : names) {
        if (uintptr_t addr = findSdkFunction(n)) {
            found = n;
            return addr;
        }
    }
    found = nullptr;
    return 0;
}

template <typename Fn>
Fn sdkFunction(const char* name) {
    return reinterpret_cast<Fn>(findSdkFunction(name));
}

// ---- SD card ----------------------------------------------------------------

constexpr const char* kMount = "coopsd";
constexpr const char* kConfigPath = "coopsd:/config/fe3h-coop/coop.ini";
constexpr const char* kOffsetsPath = "coopsd:/config/fe3h-coop/offsets.ini";

struct Fs {
    void (*setEnabledAutoAbort)(bool);
    Result (*mountSdCardForDebug)(const char*);
    Result (*openFile)(nn::fs::FileHandle*, const char*, int);
    Result (*getFileSize)(long*, nn::fs::FileHandle);
    Result (*readFile)(nn::fs::FileHandle, long, void*, ulong);
    void (*closeFile)(nn::fs::FileHandle);

    bool usable() const { return mountSdCardForDebug && openFile && getFileSize && readFile && closeFile; }
};

Fs findFs() {
    return {
        sdkFunction<void (*)(bool)>("_ZN2nn2fs19SetEnabledAutoAbortEb"),
        sdkFunction<Result (*)(const char*)>("_ZN2nn2fs19MountSdCardForDebugEPKc"),
        sdkFunction<Result (*)(nn::fs::FileHandle*, const char*, int)>("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci"),
        sdkFunction<Result (*)(long*, nn::fs::FileHandle)>("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE"),
        sdkFunction<Result (*)(nn::fs::FileHandle, long, void*, ulong)>("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm"),
        sdkFunction<void (*)(nn::fs::FileHandle)>("_ZN2nn2fs9CloseFileENS0_10FileHandleE"),
    };
}

bool readSdFile(const Fs& fs, const char* path, std::string& out) {
    nn::fs::FileHandle file;
    if (fs.openFile(&file, path, nn::fs::OpenMode_Read) != 0) return false;
    long size = 0;
    // The mod's heap is 256 KB (switch/build.sh); settings files are small.
    bool ok = fs.getFileSize(&size, file) == 0 && size >= 0 && size < 32 * 1024;
    if (ok) {
        out.resize(size_t(size));
        ok = fs.readFile(file, 0, out.data(), ulong(size)) == 0;
    }
    fs.closeFile(file);
    return ok;
}

// ---- State ------------------------------------------------------------------

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

// One hook per SDK function, for both the "many states" and "one state" forms.
#define COOP_NPAD_HOOKS(Style)                                                                          \
    HOOK_DEFINE_TRAMPOLINE(Style##Many) {                                                               \
        static inline bool installed = false;                                                           \
        static int Callback(nn::hid::Npad##Style##State* out, int count, const nn::hid::NpadIdType& id); \
    };                                                                                                  \
    HOOK_DEFINE_TRAMPOLINE(Style##One) {                                                                \
        static inline bool installed = false;                                                           \
        static void Callback(nn::hid::Npad##Style##State* out, const nn::hid::NpadIdType& id);         \
    };

COOP_NPAD_HOOKS(FullKey)
COOP_NPAD_HOOKS(Handheld)
COOP_NPAD_HOOKS(JoyDual)

// Read `count` states of one style from controller `id` without going through
// our hooks, using whichever form of the SDK function the game has.
template <typename Many, typename One, typename State>
int readOriginal(State* out, int count, nn::hid::NpadIdType id) {
    if (Many::installed) return Many::Orig(out, count, id);
    if (One::installed && count > 0) {
        One::Orig(out, id);
        return 1;
    }
    return 0;
}

int readStyle(nn::hid::NpadFullKeyState* out, int count, nn::hid::NpadIdType id) {
    return readOriginal<FullKeyMany, FullKeyOne>(out, count, id);
}
int readStyle(nn::hid::NpadHandheldState* out, int count, nn::hid::NpadIdType id) {
    return readOriginal<HandheldMany, HandheldOne>(out, count, id);
}
int readStyle(nn::hid::NpadJoyDualState* out, int count, nn::hid::NpadIdType id) {
    return readOriginal<JoyDualMany, JoyDualOne>(out, count, id);
}

template <typename State>
int relay(State* out, int count, nn::hid::NpadIdType id) {
    if (!g_router) lazyInit();

    auto d = g_router->route(id);
    if (d.route == coop::InputRouter::Route::Passthrough) {
        int n = readStyle(out, count, id);
        applyActive(out, n);
        return n;
    }
    if (d.route == coop::InputRouter::Route::Idle) {
        int n = readStyle(out, count, id);
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

    int n = readStyle(out, count, target);
    if (!connected(n) && !std::is_same_v<State, nn::hid::NpadFullKeyState>) {
        int got = readStyle(static_cast<nn::hid::NpadFullKeyState*>(base), count, target);
        if (got > 0) n = got;
    }
    if (!connected(n) && !std::is_same_v<State, nn::hid::NpadJoyDualState>) {
        int got = readStyle(static_cast<nn::hid::NpadJoyDualState*>(base), count, target);
        if (got > 0) n = got;
    }
    applyActive(out, n);
    return n;
}

#define COOP_NPAD_CALLBACKS(Style)                                                                                \
    int Style##Many::Callback(nn::hid::Npad##Style##State* out, int count, const nn::hid::NpadIdType& id) {       \
        return relay(out, count, id);                                                                             \
    }                                                                                                             \
    void Style##One::Callback(nn::hid::Npad##Style##State* out, const nn::hid::NpadIdType& id) { relay(out, 1, id); }

COOP_NPAD_CALLBACKS(FullKey)
COOP_NPAD_CALLBACKS(Handheld)
COOP_NPAD_CALLBACKS(JoyDual)

template <typename Hook>
void installNpadHook(const char* label, std::initializer_list<const char*> names) {
    const char* found = nullptr;
    if (uintptr_t addr = findSdkFunction(names, found)) {
        Hook::InstallAtPtr(addr);
        Hook::installed = true;
        Logging.Log("[coop] hooked %s", label);
    } else {
        Logging.Log("[coop] this game has no %s (tried %s)", label, *names.begin());
    }
}

[[maybe_unused]] void installControllerRelay() {
    installNpadHook<FullKeyMany>("GetNpadStates(FullKey)", {"_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj",
                                                            "_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKNS0_10NpadIdTypeE"});
    installNpadHook<HandheldMany>("GetNpadStates(Handheld)", {"_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj",
                                                              "_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKNS0_10NpadIdTypeE"});
    installNpadHook<JoyDualMany>("GetNpadStates(JoyDual)", {"_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj",
                                                            "_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKNS0_10NpadIdTypeE"});
    installNpadHook<FullKeyOne>("GetNpadState(FullKey)", {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",
                                                          "_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKNS0_10NpadIdTypeE"});
    installNpadHook<HandheldOne>("GetNpadState(Handheld)", {"_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj",
                                                            "_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKNS0_10NpadIdTypeE"});
    installNpadHook<JoyDualOne>("GetNpadState(JoyDual)", {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj",
                                                          "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKNS0_10NpadIdTypeE"});
}

// ---- Game functions (from offsets.ini) -------------------------------------

using CharacterIdFn = int (*)(void* unit, u64, u64, u64);
CharacterIdFn g_unitCharacterId = nullptr;
coop::FieldLocation g_characterIdField;  // used when there's no function to call

bool knowsCharacters() { return g_unitCharacterId || g_characterIdField.set; }

std::string characterOf(void* unit) {
    int id = g_unitCharacterId ? g_unitCharacterId(unit, 0, 0, 0) : int(coop::readField(unit, g_characterIdField));
    return coop::characterNameForGameId(id);
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
        if (knowsCharacters()) {
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
    g_characterIdField = parsed.characterIdField;
    if (g_characterIdField.set) {
        Logging.Log("[coop] reading character ids from unit+0x%x (%d bytes)", unsigned(g_characterIdField.offset),
                    g_characterIdField.size);
    }

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
    coop::Settings settings = coop::defaultSettings();
    std::string config, offsets;
    bool haveOffsets = false;

    Fs fs = findFs();
    if (!fs.usable()) {
        Logging.Log("[coop] this game's SDK has no SD card functions; using default settings");
    } else {
        // nn::fs aborts the whole game on some errors unless told not to.
        if (fs.setEnabledAutoAbort) fs.setEnabledAutoAbort(false);
        if (fs.mountSdCardForDebug(kMount) != 0) {
            Logging.Log("[coop] can't open the SD card; using default settings");
        } else {
            if (!readSdFile(fs, kConfigPath, config)) {
                Logging.Log("[coop] no %s; using default settings", kConfigPath);
            } else {
                auto result = coop::parseSettings(config);
                for (const auto& e : result.errors) Logging.Log("[coop] coop.ini error %s", e.c_str());
                for (const auto& w : result.warnings) Logging.Log("[coop] coop.ini %s", w.c_str());
                settings = std::move(result.settings);
            }
            haveOffsets = readSdFile(fs, kOffsetsPath, offsets);
        }
        if (fs.setEnabledAutoAbort) fs.setEnabledAutoAbort(true);
    }

    g_state = new coop::CoopState(std::move(settings));
    Logging.Log("[coop] %d players ready. Hold ZL + ZR and press Plus to pass the turn.", g_state->playerCount());

    if (haveOffsets) installGameHooks(offsets);
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
#ifdef FE3H_COOP_BARE
    Logging.Log("[coop] bare test module loaded; doing nothing else");
#else
    Logging.Log("[coop] fe3h-coop loaded");
    installControllerRelay();
#endif
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("fe3h-coop: unexpected exception");
}
