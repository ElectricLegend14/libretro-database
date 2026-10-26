#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace coop {

struct Color {
    uint8_t r = 0, g = 0, b = 0;

    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }

    // "#RRGGBB"
    std::string toHex() const;

    // Packed 0xAARRGGBB with full alpha, handy for tinting UI sprites.
    uint32_t toArgb() const { return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
};

// Accepts "#RRGGBB", "RRGGBB", or a preset name such as "blue" (case-insensitive).
std::optional<Color> parseColor(std::string_view text);

// Default color for a 1-based player slot (blue, red, green, yellow).
Color defaultColorFor(int player);

// Rough perceptual distance (0 = identical). Used to warn when two players
// picked colors that would be hard to tell apart on the map.
double colorDistance(const Color& a, const Color& b);

// Below this distance two cursors are considered too similar.
constexpr double kMinDistinctColorDistance = 80.0;

}  // namespace coop
