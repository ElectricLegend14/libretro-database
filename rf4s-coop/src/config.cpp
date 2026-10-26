#include "coop/config.hpp"

#include <charconv>
#include <sstream>

namespace coop {
namespace {

std::string trim(const std::string& s) {
  auto first = s.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  auto last = s.find_last_not_of(" \t\r");
  return s.substr(first, last - first + 1);
}

template <typename T>
bool parseNumber(const std::string& s, T& out) {
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc() && end == s.data() + s.size();
}

}  // namespace

std::optional<ModConfig> parseConfig(const std::string& text, std::string& error) {
  ModConfig config;
  std::istringstream lines(text);
  std::string line;
  int lineNo = 0;
  while (std::getline(lines, line)) {
    ++lineNo;
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      error = "line " + std::to_string(lineNo) + ": expected key = value";
      return std::nullopt;
    }
    std::string key = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));
    auto bad = [&] {
      error = "line " + std::to_string(lineNo) + ": bad value for " + key;
      return std::nullopt;
    };

    if (key == "role") {
      if (value == "host") config.role = Role::Host;
      else if (value == "guest") config.role = Role::Guest;
      else return bad();
    } else if (key == "port") {
      if (!parseNumber(value, config.session.port) || config.session.port == 0) return bad();
    } else if (key == "host_address") {
      config.session.hostAddress = value;
    } else if (key == "save_dir") {
      config.saveDir = value;
    } else if (key == "backup_keep") {
      if (!parseNumber(value, config.backupKeep) || config.backupKeep > 100) return bad();
    } else if (key == "name") {
      if (value.empty()) return bad();
      config.session.playerName = value;
    }
  }
  if (config.role == Role::Guest && config.session.hostAddress.empty()) {
    error = "host_address is required when role = guest";
    return std::nullopt;
  }
  return config;
}

}  // namespace coop
