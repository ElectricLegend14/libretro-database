#include "coop/color.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace coop {

namespace {

struct Preset {
    const char* name;
    Color color;
};

constexpr std::array<Preset, 12> kPresets{{
    {"blue", {0x3B, 0x82, 0xF6}},
    {"red", {0xEF, 0x44, 0x44}},
    {"green", {0x22, 0xC5, 0x5E}},
    {"yellow", {0xEA, 0xB3, 0x08}},
    {"purple", {0xA8, 0x55, 0xF7}},
    {"orange", {0xF9, 0x73, 0x16}},
    {"pink", {0xEC, 0x48, 0x99}},
    {"cyan", {0x06, 0xB6, 0xD4}},
    {"white", {0xF5, 0xF5, 0xF5}},
    // House colors
    {"eagle", {0xB9, 0x1C, 0x1C}},
    {"lion", {0x1D, 0x4E, 0xD8}},
    {"deer", {0xCA, 0x8A, 0x04}},
}};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = char(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

std::string Color::toHex() const {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", r, g, b);
    return buf;
}

std::optional<Color> parseColor(std::string_view text) {
    for (const auto& p : kPresets) {
        if (iequals(text, p.name)) return p.color;
    }

    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return std::nullopt;

    uint8_t bytes[3];
    for (int i = 0; i < 3; ++i) {
        int hi = hexDigit(text[i * 2]);
        int lo = hexDigit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        bytes[i] = uint8_t(hi * 16 + lo);
    }
    return Color{bytes[0], bytes[1], bytes[2]};
}

Color defaultColorFor(int player) {
    static constexpr const char* kOrder[] = {"blue", "red", "green", "yellow"};
    int idx = (player >= 1 && player <= 4) ? player - 1 : 0;
    return *parseColor(kOrder[idx]);
}

double colorDistance(const Color& a, const Color& b) {
    // "Redmean" approximation: cheap and good enough for a similarity warning.
    double rmean = (a.r + b.r) / 2.0;
    double dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return std::sqrt((2 + rmean / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rmean) / 256) * db * db);
}

}  // namespace coop
