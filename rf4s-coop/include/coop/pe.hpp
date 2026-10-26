#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/layout.hpp"

namespace coop {

// Just enough of the PE format to find a 64-bit Windows executable's
// sections on disk, so signatures can be checked without running the game.
struct PeSection {
  std::string name;
  uint32_t rva = 0;
  uint32_t virtualSize = 0;
  uint32_t rawOffset = 0;
  uint32_t rawSize = 0;
  bool executable = false;
};

struct PeImage {
  uint32_t timestamp = 0;  // what rf4s-coop.log calls the game build
  std::vector<PeSection> sections;

  const PeSection* codeSection() const;  // the largest executable one
  const PeSection* sectionAt(uint32_t rva) const;
};

std::optional<PeImage> parsePe(const std::vector<uint8_t>& file, std::string& error);

// What one layout entry resolves to in an executable on disk.
struct LayoutCheck {
  std::string name;
  bool hasSignature = false;
  size_t matches = 0;
  uint32_t matchRva = 0;   // valid when matches == 1
  uint32_t targetRva = 0;  // the global it loads, or the function's start
  std::string targetSection;
  bool ok() const { return !hasSignature || (matches == 1 && !targetSection.empty()); }
};

// Checks the player, map, clock and frame signatures of `layout` against the
// executable, the same way the DLL does in memory.
std::vector<LayoutCheck> checkLayout(const PeImage& pe, const std::vector<uint8_t>& file, const GameLayout& layout);

}  // namespace coop
