// Minimal self-contained test runner so the core builds with no dependencies.

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "coop/coop_state.hpp"
#include "coop/hook_targets.hpp"
#include "coop/input_router.hpp"
#include "coop/nso.hpp"
#include "coop/roster.hpp"
#include "coop/settings.hpp"

static int g_failures = 0;

#define CHECK(...)                                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) {                                               \
            std::cerr << __FILE__ << ":" << __LINE__                        \
                      << ": CHECK(" #__VA_ARGS__ ")\n";                     \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

static bool hasMessage(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs)
        if (m.find(needle) != std::string::npos) return true;
    return false;
}

static void testColors() {
    CHECK(coop::parseColor("#FF8000") == (coop::Color{0xFF, 0x80, 0x00}));
    CHECK(coop::parseColor("ff8000") == (coop::Color{0xFF, 0x80, 0x00}));
    CHECK(coop::parseColor("Blue").has_value());
    CHECK(coop::parseColor("lion").has_value());
    CHECK(!coop::parseColor("#GG0000"));
    CHECK(!coop::parseColor("#FFF"));
    CHECK(coop::Color{0x12, 0xAB, 0x00}.toHex() == "#12AB00");
    CHECK(coop::Color{0x12, 0xAB, 0x00}.toArgb() == 0xFF12AB00u);
}

static void testRoster() {
    CHECK(coop::resolveCharacter("edelgard") == std::optional<std::string>("Edelgard"));
    CHECK(coop::resolveCharacter("#42") == std::optional<std::string>("#42"));
    CHECK(!coop::resolveCharacter("#4x"));
    CHECK(!coop::resolveCharacter("Marth"));
    // Ids aren't mapped yet, so units fall back to "#id", which configs can use.
    CHECK(coop::characterNameForGameId(7) == "#7");
    CHECK(coop::resolveCharacter("#7") == std::optional<std::string>(coop::characterNameForGameId(7)));
}

static void testSampleConfig() {
    std::ifstream in(COOP_SAMPLE_CONFIG);
    std::stringstream ss;
    ss << in.rdbuf();
    auto r = coop::parseSettings(ss.str());
    CHECK(r.ok());
    CHECK(r.warnings.empty());
    CHECK(r.settings.players.size() == 2);
    CHECK(r.settings.players[0].characters.size() == 4);
    CHECK(r.settings.players[1].controller == 1);
}

static void testValidation() {
    auto r = coop::parseSettings(
        "[coop]\nplayers = 3\n"
        "[player1]\ncolor = #3B82F6\ncharacters = Dimitri, Felix, Marth\n"
        "[player2]\ncolor = #3C83F7\ncharacters = felix, Ingrid\n"
        "[player3]\ncolor = nope\n"
        "[player4]\nname = Extra\n");
    CHECK(r.ok());
    const auto& p = r.settings.players;
    CHECK(p.size() == 3);
    CHECK(p[0].characters == (std::vector<std::string>{"Dimitri", "Felix"}));
    CHECK(p[1].characters == (std::vector<std::string>{"Ingrid"}));  // Felix kept by player 1
    CHECK(p[2].color == coop::defaultColorFor(3));
    CHECK(hasMessage(r.warnings, "unknown character 'Marth'"));
    CHECK(hasMessage(r.warnings, "Felix is already claimed by player 1"));
    CHECK(hasMessage(r.warnings, "unknown color 'nope'"));
    CHECK(hasMessage(r.warnings, "[player4] ignored"));
    CHECK(hasMessage(r.warnings, "very similar colors"));

    auto bad = coop::parseSettings("[coop]\nplayers = 9\n");
    CHECK(!bad.ok());
    CHECK(bad.settings.players.size() == 2);
}

static void testOwnershipAndTurns() {
    auto r = coop::parseSettings(
        "[coop]\nunclaimed_units = locked\n"
        "[player1]\ncontroller = 1\ncharacters = Claude, Hilda\n"
        "[player2]\ncontroller = 1\ncharacters = Lorenz\n");
    coop::CoopState s(r.settings);

    CHECK(s.activePlayer() == 1);
    CHECK(s.canSelect("Claude"));
    CHECK(!s.canSelect("Lorenz"));
    CHECK(!s.canSelect("Leonie"));  // unclaimed + locked
    CHECK(s.acceptsInput(0));       // shared controller: hot-seat
    s.nextPlayer();
    CHECK(s.activePlayer() == 2);
    CHECK(s.canSelect("Lorenz"));
    CHECK(!s.canSelect("Claude"));
    s.nextPlayer();
    CHECK(s.activePlayer() == 1);

    CHECK(s.outlineColorOf("Hilda") == s.player(1).color);
    CHECK(!s.outlineColorOf("Leonie"));
}

static void testClaimingRoundTrip() {
    coop::CoopState s(coop::defaultSettings());
    CHECK(s.toggleClaim(2, "Lysithea"));
    CHECK(s.ownerOf("Lysithea") == 2);
    CHECK(!s.claim(1, "Lysithea"));  // can't steal another player's unit
    CHECK(s.dirty());
    CHECK(s.toggleClaim(2, "Lysithea"));  // release
    CHECK(!s.ownerOf("Lysithea"));
    CHECK(s.claim(1, "Lysithea"));

    auto text = coop::serializeSettings(s.settings());
    auto back = coop::parseSettings(text);
    CHECK(back.ok());
    CHECK(back.warnings.empty());
    CHECK(back.settings.players[0].characters == (std::vector<std::string>{"Lysithea"}));
    CHECK(back.settings.players[1].color == s.player(2).color);

    auto locked = coop::defaultSettings();
    locked.allowInGameClaiming = false;
    coop::CoopState noClaims(locked);
    CHECK(!noClaims.toggleClaim(1, "Raphael"));
}

static void testTurnOrder() {
    auto r = coop::parseSettings(
        "[coop]\nplayers = 3\nunclaimed_units = locked\n"
        "[player1]\ncontroller = 1\ncharacters = Claude\n"
        "[player2]\ncontroller = 2\ncharacters = Lorenz\n"
        "[player3]\ncontroller = 1\ncharacters = Hilda\n");
    coop::CoopState s(r.settings);

    // Lorenz isn't deployed, so player 2 sits this map out.
    s.beginPlayerPhase({"Claude", "Hilda", "Leonie"});
    CHECK(s.turnOrder() == (std::vector<int>{1, 3}));
    CHECK(s.activePlayer() == 1);
    auto next = s.endTurn();
    CHECK(!next.phaseOver);
    CHECK(next.player == 3);
    CHECK(next.passController);  // players 1 and 3 share controller 1
    CHECK(s.endTurn().phaseOver);

    // Nothing movable: player 1 still gets the turn so the game can't stall.
    s.beginPlayerPhase({"Leonie"});
    CHECK(s.turnOrder() == (std::vector<int>{1}));
    CHECK(s.endTurn().phaseOver);

    auto shared = coop::parseSettings("[player2]\ncharacters = Lorenz\n");
    coop::CoopState t(shared.settings);
    t.beginPlayerPhase({"Leonie"});  // unclaimed + shared: everyone may move Leonie
    CHECK(t.turnOrder() == (std::vector<int>{1, 2}));
}

static void testInputRouter() {
    using coop::InputRouter;
    namespace b = coop::button;
    coop::CoopState s(coop::defaultSettings());  // controllers 1 and 2
    InputRouter router(s);

    CHECK(router.route(coop::kNpadNo1).route == InputRouter::Route::Passthrough);
    CHECK(router.route(coop::kNpadHandheld).route == InputRouter::Route::Passthrough);
    CHECK(router.route(1).route == InputRouter::Route::Idle);

    CHECK(!router.observe(b::ZL | b::ZR));
    auto pass = router.observe(b::ZL | b::ZR | b::Plus);
    CHECK(pass && pass->player == 2 && !pass->passController);
    CHECK(s.activePlayer() == 2);
    CHECK(!router.observe(b::ZL | b::ZR | b::Plus));  // still held: no double pass

    auto d = router.route(coop::kNpadHandheld);
    CHECK(d.route == InputRouter::Route::Redirect && d.npadId == 1);
    CHECK(router.route(1).route == InputRouter::Route::Idle);

    router.observe(0);
    CHECK(router.observe(b::ZL | b::ZR | b::Plus));
    CHECK(s.activePlayer() == 1);

    CHECK(InputRouter::mask(b::ZL | b::ZR | b::Plus | b::A) == (b::ZL | b::ZR | b::A));
    CHECK(InputRouter::mask(b::Plus | b::ZL) == (b::Plus | b::ZL));
}

static void testSigscan() {
    CHECK(coop::parsePattern("FD 7b ?? A9").has_value());
    CHECK(coop::parsePattern("FD 7B ? A9")->size() == 4);
    CHECK(coop::parsePattern("9? ?4")->mask == (std::vector<uint8_t>{0xF0, 0x0F}));
    CHECK(!coop::parsePattern("FD7B"));
    CHECK(!coop::parsePattern("FD 7G"));
    CHECK(!coop::parsePattern("?? ??"));
    CHECK(!coop::parsePattern(""));

    uint8_t code[24] = {0x11, 0x22, 0xAA, 0xBB, 0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD,
                        0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xEE, 0, 0, 0, 0};
    auto r = coop::scanUnique(code, sizeof code, *coop::parsePattern("AA BB CC DD"));
    CHECK(r.status == coop::ScanResult::Found && r.offset == 8);
    CHECK(coop::scanUnique(code, sizeof code, *coop::parsePattern("AA BB CC ??")).status ==
          coop::ScanResult::Ambiguous);
    // The AA BB at offset 2 is unaligned, so it doesn't count.
    CHECK(coop::scanUnique(code, sizeof code, *coop::parsePattern("AA BB 00 00")).status ==
          coop::ScanResult::NotFound);

    // Nibble wildcards: AA B? matches both AA BB and AA BC.
    const uint8_t two[] = {0xAA, 0xBB, 0, 0, 0xAA, 0xBC, 0, 0};
    CHECK(coop::scanUnique(two, sizeof two, *coop::parsePattern("AA B? 00 00")).status ==
          coop::ScanResult::Ambiguous);
    CHECK(coop::scanUnique(two, sizeof two, *coop::parsePattern("AA ?C 00 00")).offset == 4);

    // A "call:" pattern's leading BL matches forward (94) and backward (97) calls.
    auto bl = *coop::parsePattern("?? ?? ?? ?? 11");
    CHECK(coop::requireLeadingBl(bl));
    const uint8_t calls[] = {0x04, 0x00, 0x00, 0x94, 0x11, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x97, 0x11, 0, 0, 0};
    CHECK(coop::scanUnique(calls, sizeof calls, bl).status == coop::ScanResult::Ambiguous);
    CHECK(coop::scanUnique(calls + 8, 8, bl).status == coop::ScanResult::Found);
    const uint8_t notCall[] = {0x1F, 0x20, 0x03, 0xD5, 0x11, 0, 0, 0};  // nop, not a bl
    CHECK(coop::scanUnique(notCall, sizeof notCall, bl).status == coop::ScanResult::NotFound);
    auto shortBl = *coop::parsePattern("11 22");
    CHECK(!coop::requireLeadingBl(shortBl));

    CHECK(coop::branchTarget(0x94000004u, 0x1000) == std::optional<uint64_t>(0x1010));  // bl +0x10
    CHECK(coop::branchTarget(0x97FFFFFFu, 0x1000) == std::optional<uint64_t>(0xFFC));   // bl -4
    CHECK(coop::branchTarget(0x14000002u, 0x2000) == std::optional<uint64_t>(0x2008));  // b +8
    CHECK(!coop::branchTarget(0xD65F03C0u, 0x1000));                                     // ret
}

static void testHookTargets() {
    auto r = coop::parseHookTargets(
        "[hooks]\n"
        "can_select_unit = 0x108\n"
        "end_player_phase = pattern: E0 03 1F 2A\n"
        "unit_character_id = call: ?? ?? ?? ?? C0 03\n"
        "draw_map_cursor =\n"
        "made_up = 0x10\n"
        "begin_player_phase = nope\n");
    CHECK(r.targets.size() == 3);
    CHECK(hasMessage(r.warnings, "unknown hook 'made_up'"));
    CHECK(hasMessage(r.warnings, "begin_player_phase: expected a hex offset"));

    // Fake .text starting 0x100 into the module:
    //   +0x00 mov w0, wzr       (E0 03 1F 2A)
    //   +0x04 bl  +0x0C         (03 00 00 94) -> 0x110
    //   +0x08 ret               (C0 03 5F D6)
    const uint8_t text[] = {0xE0, 0x03, 0x1F, 0x2A, 0x03, 0x00, 0x00, 0x94, 0xC0, 0x03, 0x5F, 0xD6,
                            0x1F, 0x20, 0x03, 0xD5, 0xC0, 0x03, 0x5F, 0xD6};
    const uint64_t base = 0x100;
    auto find = [&](const std::string& name) -> const coop::HookTarget& {
        for (const auto& t : r.targets)
            if (t.name == name) return t;
        return r.targets.front();
    };

    auto off = coop::resolveHook(find("can_select_unit"), text, sizeof text, base);
    CHECK(off.ok && off.offset == 0x108);
    auto pat = coop::resolveHook(find("end_player_phase"), text, sizeof text, base);
    CHECK(pat.ok && pat.offset == 0x100);
    auto call = coop::resolveHook(find("unit_character_id"), text, sizeof text, base);
    CHECK(call.ok && call.offset == 0x110);

    coop::HookTarget outside{"can_select_unit", coop::HookTarget::Kind::Offset, 0x9000, {}, 1};
    CHECK(!coop::resolveHook(outside, text, sizeof text, base).ok);
    coop::HookTarget misaligned{"can_select_unit", coop::HookTarget::Kind::Offset, 0x102, {}, 1};
    CHECK(!coop::resolveHook(misaligned, text, sizeof text, base).ok);

    // [units] field fallback for an inlined character-id getter.
    auto units = coop::parseHookTargets("[units]\ncharacter_id_field = 0x6\ncharacter_id_size = 2\n");
    CHECK(units.characterIdField.set && units.characterIdField.offset == 6 && units.characterIdField.size == 2);
    CHECK(units.warnings.empty());
    const uint8_t unit[8] = {0, 0, 0, 0, 0, 0, 0x34, 0x12};
    CHECK(coop::readField(unit, units.characterIdField) == 0x1234);
    CHECK(coop::readField(unit, {true, 6, 1}) == 0x34);
    auto badUnits = coop::parseHookTargets("[units]\ncharacter_id_field = 0x6\ncharacter_id_size = 3\nhp = 1\n");
    CHECK(badUnits.characterIdField.set && badUnits.characterIdField.size == 2);
    CHECK(hasMessage(badUnits.warnings, "character_id_size must be 1, 2 or 4"));
    CHECK(hasMessage(badUnits.warnings, "unknown option 'hp'"));
    CHECK(!coop::parseHookTargets("[units]\ncharacter_id_field = lots\n").characterIdField.set);

    std::ifstream in(COOP_SAMPLE_OFFSETS);
    std::stringstream ss;
    ss << in.rdbuf();
    auto sample = coop::parseHookTargets(ss.str());
    CHECK(sample.targets.empty());
    CHECK(sample.warnings.empty());
}

static std::vector<uint8_t> readBytes(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void testNso() {
    auto file = readBytes(COOP_SAMPLE_NSO);
    CHECK(file.size() > 0x100);

    coop::NsoText nso;
    std::string error;
    CHECK(coop::loadNsoText(file.data(), file.size(), nso, error));
    CHECK(error.empty());
    CHECK(nso.code.size() == 4096);
    CHECK(nso.textOffset == 0);
    CHECK(nso.buildId[0] == 0xA0 && nso.buildId[31] == 0xBF);
    CHECK(nso.code[0] == 0x1F && nso.code[4095] == 0xD5);  // nops at both ends
    CHECK(nso.code[0x800] == 0xE0 && nso.code[0x903] == 0x97);  // bl backwards

    // Patterns resolve against the decompressed code like they would in-game.
    auto hooks = coop::parseHookTargets(
        "[hooks]\nend_player_phase = pattern: E0 03 1F 2A C0 03 5F D6\n"
        "unit_character_id = call: ?? ?? ?? ?? 1F 20 03 D5\n");  // bl, then a nop
    CHECK(hooks.targets.size() == 2);
    for (const auto& t : hooks.targets) {
        auto r = coop::resolveHook(t, nso.code.data(), nso.code.size(), nso.textOffset);
        CHECK(r.ok && r.offset == 0x800);
    }

    // Broken files fail cleanly.
    CHECK(!coop::loadNsoText(file.data(), 0x80, nso, error));
    auto badMagic = file;
    badMagic[0] = 'X';
    CHECK(!coop::loadNsoText(badMagic.data(), badMagic.size(), nso, error));
    auto truncated = std::vector<uint8_t>(file.begin(), file.end() - 10);
    CHECK(!coop::loadNsoText(truncated.data(), truncated.size(), nso, error));

    // LZ4 edge cases: literals only, overlapping match, bad back-reference.
    const uint8_t literals[] = {0x30, 'a', 'b', 'c'};
    uint8_t out[16];
    CHECK(coop::lz4DecompressBlock(literals, sizeof literals, out, 3) && out[2] == 'c');
    CHECK(!coop::lz4DecompressBlock(literals, sizeof literals, out, 4));  // size mismatch
    const uint8_t overlap[] = {0x14, 'x', 0x01, 0x00, 0x00};  // "x" then copy 8 from 1 back, then end
    CHECK(coop::lz4DecompressBlock(overlap, sizeof overlap, out, 9) && out[8] == 'x');
    const uint8_t badRef[] = {0x10, 'x', 0x05, 0x00, 0x00};
    CHECK(!coop::lz4DecompressBlock(badRef, sizeof badRef, out, 9));
}

int main() {
    testNso();
    testTurnOrder();
    testInputRouter();
    testSigscan();
    testHookTargets();
    testColors();
    testRoster();
    testSampleConfig();
    testValidation();
    testOwnershipAndTurns();
    testClaimingRoundTrip();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
