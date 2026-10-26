// Minimal self-contained test runner so the core builds with no dependencies.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "coop/coop_state.hpp"
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

int main() {
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
