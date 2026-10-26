#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "coop/game_adapter.hpp"
#include "coop/layout.hpp"

namespace coop {

// The real game, read and written through process memory using the
// signatures and offsets in a GameLayout.
class Rf4sAdapter : public IGameAdapter {
 public:
  using Log = std::function<void(const std::string&)>;

  Rf4sAdapter(GameLayout layout, Log log) : layout_(std::move(layout)), log_(std::move(log)) {}

  // Finds every signature in the executable's code section and logs what's
  // missing. Offsets are relative to `module`. Returns true when everything a
  // session needs was found.
  bool resolve(const uint8_t* module, size_t imageSize, size_t codeStart, size_t codeSize);
  bool hasPlayer() const { return player_ != 0; }
  // The game's per-frame update, if its signature was found.
  void* frameFunction() const { return frame_; }

  // Hex lines of the player object's first `bytes` bytes, for finding offsets.
  std::vector<std::string> dumpPlayer(size_t bytes);

  PlayerSnapshot readLocalPlayer() override;
  GameClock readClock() override;
  void writeClock(const GameClock& clock) override;
  void showRemotePlayer(const PlayerSnapshot& remote) override;
  void hideRemotePlayer() override;
  void warpLocalPlayer(uint16_t mapId, const Vec3& pos) override;

 private:
  GameLayout layout_;
  Log log_;
  uintptr_t player_ = 0;  // address of the global that holds the player pointer
  uintptr_t mapId_ = 0;
  uintptr_t clock_ = 0;
  void* frame_ = nullptr;
  PlayerSnapshot last_;
  GameClock lastClock_;
  int framesSinceLog_ = 0;
  bool remoteShown_ = false;
};

}  // namespace coop
