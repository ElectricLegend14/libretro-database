#pragma once

#include <functional>
#include <string>

namespace coop {

// Runs a callback inside the game's own per-frame update, on the game's
// thread, so co-op reads and writes never race the game's own logic. The
// callback runs just before the original, which then sees its arguments and
// return path untouched, whatever its signature.
class FrameHook {
 public:
  static bool install(void* target, std::function<void()> onFrame, std::string& error);
  // Waits for a callback in progress to finish. Safe to call more than once.
  static void uninstall();
};

}  // namespace coop
