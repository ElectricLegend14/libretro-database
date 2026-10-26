#pragma once

// Where things live inside Rune Factory 4 Special, as compiled into the DLL.
// Every value comes from reverse engineering (see "Reverse-engineering plan"
// in the design doc) and none is known yet.
//
// While hunting, put guesses in rf4s-layout.ini next to the DLL instead: it
// overrides these at startup, so no rebuild is needed (keys in
// include/coop/layout.hpp, example in rf4s-layout.example.ini). Check each
// signature with sigscan first: it must match exactly once in the game's
// executable. Once a value is confirmed, copy it here.

#include "coop/layout.hpp"

namespace coop::rf4s {

inline GameLayout defaultLayout() {
  GameLayout layout;
  // layout.player.signature = "...";  layout.playerFields.pos = 0x...;
  return layout;
}

}  // namespace coop::rf4s
