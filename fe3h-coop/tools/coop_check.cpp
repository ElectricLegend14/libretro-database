// Check coop.ini (and optionally offsets.ini) on your PC before copying them
// to the SD card.
//
//   coop_check coop.ini [offsets.ini [main]]
//
// With the game's exefs "main", every hook in offsets.ini is looked up in the
// real code, so you know a pattern matches exactly once before you boot.
//
// Exit code 0 = ready to use, 1 = something needs fixing first.

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "coop/hook_targets.hpp"
#include "coop/nso.hpp"
#include "coop/settings.hpp"

namespace {

bool g_color = false;

std::string readFile(const char* path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = bool(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string swatch(const coop::Color& c) {
    if (!g_color) return "";
    return "\x1b[48;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m  \x1b[0m ";
}

const char* policyText(coop::UnclaimedPolicy p) {
    switch (p) {
        case coop::UnclaimedPolicy::Shared: return "anyone can move them";
        case coop::UnclaimedPolicy::Host: return "only player 1 can move them";
        case coop::UnclaimedPolicy::Locked: return "nobody can move them";
    }
    return "";
}

void printList(const char* heading, const std::vector<std::string>& items) {
    if (items.empty()) return;
    std::cout << "\n" << heading << "\n";
    for (const auto& i : items) std::cout << "  - " << i << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: " << argv[0] << " coop.ini [offsets.ini [main]]\n";
        return 2;
    }
    g_color = isatty(STDOUT_FILENO);

    bool ok = false;
    auto text = readFile(argv[1], ok);
    if (!ok) {
        std::cerr << "Can't open " << argv[1] << "\n";
        return 2;
    }

    auto result = coop::parseSettings(text);
    const auto& s = result.settings;

    std::cout << argv[1] << "\n";
    if (result.ok()) {
        std::cout << "\n" << s.players.size() << " players\n";
        for (size_t i = 0; i < s.players.size(); ++i) {
            const auto& p = s.players[i];
            std::cout << "  " << swatch(p.color) << "P" << i + 1 << " " << p.name << "  " << p.color.toHex()
                      << "  controller " << p.controller + 1 << "\n      ";
            if (p.characters.empty()) std::cout << "(no units yet)";
            for (size_t c = 0; c < p.characters.size(); ++c) std::cout << (c ? ", " : "") << p.characters[c];
            std::cout << "\n";
        }
        std::cout << "\nUnclaimed units: " << policyText(s.unclaimed) << "\n"
                  << "Claim during battle prep: " << (s.allowInGameClaiming ? "on" : "off") << "\n"
                  << "Colored outlines: " << (s.tintUnitOutlines ? "on" : "off") << "\n";
    }
    printList("Errors (the game will use default settings):", result.errors);
    printList("Warnings:", result.warnings);

    bool hooksOk = true;
    if (argc >= 3) {
        auto offsetsText = readFile(argv[2], ok);
        if (!ok) {
            std::cerr << "Can't open " << argv[2] << "\n";
            return 2;
        }
        auto hooks = coop::parseHookTargets(offsetsText);

        coop::NsoText nso;
        bool haveMain = false;
        if (argc == 4) {
            auto mainBytes = readFile(argv[3], ok);
            std::string error;
            if (!ok) {
                std::cerr << "Can't open " << argv[3] << "\n";
                return 2;
            }
            if (!coop::loadNsoText(reinterpret_cast<const uint8_t*>(mainBytes.data()), mainBytes.size(), nso, error)) {
                std::cerr << argv[3] << ": " << error << "\n";
                return 2;
            }
            haveMain = true;
            std::cout << "\n" << argv[3] << "\n  build id ";
            for (auto b : nso.buildId) {
                char hex[3];
                std::snprintf(hex, sizeof hex, "%02X", b);
                std::cout << hex;
            }
            std::cout << "\n  " << nso.code.size() / 1024 << " KiB of code\n";
        }

        std::cout << "\n" << argv[2] << "\n";
        for (const auto& known : coop::knownHooks()) {
            const coop::HookTarget* found = nullptr;
            for (const auto& t : hooks.targets)
                if (t.name == known.name) found = &t;
            const char* how = !found                                          ? "not set yet"
                              : found->kind == coop::HookTarget::Kind::Offset ? "offset"
                              : found->kind == coop::HookTarget::Kind::Call   ? "call pattern"
                                                                              : "pattern";
            std::cout << "  " << (found ? "[x] " : "[ ] ") << known.name << "  (" << how << ")";
            if (found && haveMain) {
                auto r = coop::resolveHook(*found, nso.code.data(), nso.code.size(), nso.textOffset);
                if (r.ok) {
                    char at[32];
                    std::snprintf(at, sizeof at, "main+0x%llX", static_cast<unsigned long long>(r.offset));
                    std::cout << "  -> " << at;
                } else {
                    std::cout << "  -> " << r.error;
                    hooksOk = false;
                }
            }
            std::cout << "\n";
        }
        if (hooks.characterIdField.set) {
            char field[64];
            std::snprintf(field, sizeof field, "  [x] character id field  (unit+0x%X, %d bytes)\n",
                          unsigned(hooks.characterIdField.offset), hooks.characterIdField.size);
            std::cout << field;
        }
        printList("Warnings:", hooks.warnings);
    }

    bool ready = result.ok() && hooksOk;
    std::cout << (ready ? "\nReady to copy to sdmc:/config/fe3h-coop/\n" : "\nFix the problems above first.\n");
    return ready ? 0 : 1;
}
