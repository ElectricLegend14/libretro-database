#include "rf4s_adapter.hpp"

#include <windows.h>

#include <cstdio>
#include <optional>

#include "coop/signature.hpp"

namespace coop {
namespace {

// ReadProcessMemory on our own process fails cleanly on a bad address instead
// of crashing the game, which matters while the offsets are still guesses.
template <typename T>
std::optional<T> safeRead(uintptr_t address) {
  T value;
  SIZE_T got = 0;
  if (address == 0 ||
      !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof value, &got) ||
      got != sizeof value) {
    return std::nullopt;
  }
  return value;
}

template <typename T>
bool safeWrite(uintptr_t address, const T& value) {
  SIZE_T put = 0;
  return address != 0 &&
         WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof value, &put) &&
         put == sizeof value;
}

template <typename T>
void readField(uintptr_t base, ptrdiff_t offset, T& out) {
  if (offset == kUnknownOffset) return;
  if (auto v = safeRead<T>(base + static_cast<uintptr_t>(offset))) out = *v;
}

template <typename T>
void writeField(uintptr_t base, ptrdiff_t offset, const T& value) {
  if (offset != kUnknownOffset) safeWrite(base + static_cast<uintptr_t>(offset), value);
}

struct Image {
  const uint8_t* module;
  size_t size;
  size_t codeStart;
  size_t codeSize;
};

uintptr_t resolveGlobal(const Image& image, const GlobalRef& ref, const char* name, const Rf4sAdapter::Log& log) {
  if (ref.signature.empty()) {
    log(std::string("  ") + name + ": no signature yet");
    return 0;
  }
  auto pattern = parsePattern(ref.signature);
  if (!pattern) {
    log(std::string("  ") + name + ": signature doesn't parse");
    return 0;
  }
  // Only the code section is scanned: other sections may not be readable.
  auto hits = findAll(image.module + image.codeStart, image.codeSize, *pattern);
  if (hits.size() != 1) {
    log(std::string("  ") + name + ": signature matched " + std::to_string(hits.size()) + " times, need exactly 1");
    return 0;
  }
  auto target =
      resolveRipRelative(image.module, image.size, image.codeStart + hits.front(), ref.dispOffset, ref.instrLength);
  if (!target) {
    log(std::string("  ") + name + ": RIP-relative target is outside the module");
    return 0;
  }
  char line[96];
  std::snprintf(line, sizeof line, "  %s: found at +0x%zx", name, *target);
  log(line);
  return reinterpret_cast<uintptr_t>(image.module) + *target;
}

void* resolveFunction(const Image& image, const FunctionRef& ref, const char* name, const Rf4sAdapter::Log& log) {
  if (ref.signature.empty()) {
    log(std::string("  ") + name + ": no signature yet");
    return nullptr;
  }
  auto pattern = parsePattern(ref.signature);
  auto hits = pattern ? findAll(image.module + image.codeStart, image.codeSize, *pattern) : std::vector<size_t>{};
  if (hits.size() != 1) {
    log(std::string("  ") + name + ": signature matched " + std::to_string(hits.size()) + " times, need exactly 1");
    return nullptr;
  }
  auto start = static_cast<ptrdiff_t>(hits.front()) + ref.offset;
  if (start < 0 || static_cast<size_t>(start) >= image.codeSize) {
    log(std::string("  ") + name + ": offset points outside the code section");
    return nullptr;
  }
  char line[96];
  std::snprintf(line, sizeof line, "  %s: found at +0x%zx", name, image.codeStart + static_cast<size_t>(start));
  log(line);
  return const_cast<uint8_t*>(image.module + image.codeStart + start);
}

}  // namespace

bool Rf4sAdapter::resolve(const uint8_t* module, size_t imageSize, size_t codeStart, size_t codeSize) {
  log_("Resolving signatures:");
  Image image{module, imageSize, codeStart, codeSize};
  player_ = resolveGlobal(image, layout_.player, "player", log_);
  mapId_ = resolveGlobal(image, layout_.mapId, "map id", log_);
  clock_ = resolveGlobal(image, layout_.clock, "clock", log_);
  frame_ = resolveFunction(image, layout_.frame, "frame update", log_);
  return player_ && mapId_ && clock_ && layout_.playerFields.pos != kUnknownOffset;
}

PlayerSnapshot Rf4sAdapter::readLocalPlayer() {
  auto object = safeRead<uintptr_t>(player_);
  if (object && *object) {
    const auto& o = layout_.playerFields;
    readField(*object, o.pos, last_.pos);
    readField(*object, o.facing, last_.facing);
    readField(*object, o.anim, last_.anim);
    readField(*object, o.hp, last_.hp);
    readField(*object, o.maxHp, last_.maxHp);
    readField(*object, o.rp, last_.rp);
    readField(*object, o.maxRp, last_.maxRp);
  }
  if (auto map = safeRead<uint16_t>(mapId_)) last_.mapId = *map;
  return last_;  // keeps the last good values during loading screens
}

GameClock Rf4sAdapter::readClock() {
  if (auto object = safeRead<uintptr_t>(clock_); object && *object) {
    const auto& o = layout_.clockFields;
    readField(*object, o.year, lastClock_.year);
    readField(*object, o.season, lastClock_.season);
    readField(*object, o.day, lastClock_.day);
    readField(*object, o.hour, lastClock_.hour);
    readField(*object, o.minute, lastClock_.minute);
  }
  return lastClock_;
}

void Rf4sAdapter::writeClock(const GameClock& clock) {
  auto object = safeRead<uintptr_t>(clock_);
  if (!object || !*object) return;
  const auto& o = layout_.clockFields;
  writeField(*object, o.year, clock.year);
  writeField(*object, o.season, clock.season);
  writeField(*object, o.day, clock.day);
  writeField(*object, o.hour, clock.hour);
  writeField(*object, o.minute, clock.minute);
}

std::vector<std::string> Rf4sAdapter::dumpPlayer(size_t bytes) {
  std::vector<std::string> lines;
  auto object = safeRead<uintptr_t>(player_);
  if (!object || !*object) return lines;
  for (size_t row = 0; row < bytes; row += 16) {
    char line[96];
    int n = std::snprintf(line, sizeof line, "  +%04zx:", row);
    for (size_t i = row; i < row + 16 && i < bytes; ++i) {
      auto b = safeRead<uint8_t>(*object + i);
      n += std::snprintf(line + n, sizeof line - static_cast<size_t>(n), b ? " %02x" : " ??", b ? *b : 0);
    }
    lines.push_back(line);
  }
  return lines;
}

// Phase 1 draws the partner as a model in the world. Until the spawning code
// is found (RE target 4), log where they are about once a second instead.
void Rf4sAdapter::showRemotePlayer(const PlayerSnapshot& remote) {
  remoteShown_ = true;
  if (++framesSinceLog_ < 60) return;
  framesSinceLog_ = 0;
  char line[128];
  std::snprintf(line, sizeof line, "Partner on map %u at (%.1f, %.1f, %.1f), HP %u/%u", remote.mapId, remote.pos.x,
                remote.pos.y, remote.pos.z, remote.hp, remote.maxHp);
  log_(line);
}

void Rf4sAdapter::hideRemotePlayer() {
  if (remoteShown_) log_("Partner is on another map");
  remoteShown_ = false;
}

// Needs the map-loading function (RE target 6) so the game runs its own
// transition. Writing the map ID directly would skip loading the new area.
void Rf4sAdapter::warpLocalPlayer(uint16_t mapId, const Vec3&) {
  log_("Leash: would follow the host to map " + std::to_string(mapId) + " (not implemented yet)");
}

}  // namespace coop
