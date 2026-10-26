#pragma once

#include <map>
#include <optional>
#include <string>

#include "coop/session.hpp"

namespace coop {

enum class Role { Host, Guest };

struct ModConfig {
  Role role = Role::Host;
  SessionConfig session;
};

// Parses rf4s-coop.ini: `key = value` lines, `#` or `;` comments. Unknown keys
// are ignored. Returns nullopt with `error` set for a bad value.
std::optional<ModConfig> parseConfig(const std::string& text, std::string& error);

}  // namespace coop
