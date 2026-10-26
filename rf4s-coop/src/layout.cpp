#include "coop/layout.hpp"

#include <charconv>
#include <map>
#include <sstream>

#include "coop/signature.hpp"

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
  bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  const char* begin = s.data() + (hex ? 2 : 0);
  const char* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, out, hex ? 16 : 10);
  return ec == std::errc() && ptr == end && begin != end;
}

}  // namespace

bool applyLayoutOverrides(const std::string& text, GameLayout& layout, std::string& error) {
  std::map<std::string, GlobalRef*> globals = {
      {"player", &layout.player}, {"map", &layout.mapId}, {"clock", &layout.clock}};
  std::map<std::string, ptrdiff_t*> offsets = {
      {"player.pos", &layout.playerFields.pos},       {"player.facing", &layout.playerFields.facing},
      {"player.anim", &layout.playerFields.anim},     {"player.hp", &layout.playerFields.hp},
      {"player.max_hp", &layout.playerFields.maxHp},  {"player.rp", &layout.playerFields.rp},
      {"player.max_rp", &layout.playerFields.maxRp},  {"clock.year", &layout.clockFields.year},
      {"clock.season", &layout.clockFields.season},   {"clock.day", &layout.clockFields.day},
      {"clock.hour", &layout.clockFields.hour},       {"clock.minute", &layout.clockFields.minute},
  };

  std::istringstream lines(text);
  std::string line;
  int lineNo = 0;
  while (std::getline(lines, line)) {
    ++lineNo;
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    auto fail = [&](const std::string& why) {
      error = "line " + std::to_string(lineNo) + ": " + why;
      return false;
    };
    auto eq = line.find('=');
    if (eq == std::string::npos) return fail("expected key = value");
    std::string key = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));

    if (auto it = offsets.find(key); it != offsets.end()) {
      if (!parseNumber(value, *it->second) || *it->second < 0) return fail("bad offset for " + key);
      continue;
    }
    if (key == "dump_player_bytes") {
      if (!parseNumber(value, layout.dumpPlayerBytes) || layout.dumpPlayerBytes > 4096) {
        return fail("dump_player_bytes must be 0 to 4096");
      }
      continue;
    }
    auto dot = key.find('.');
    auto global = dot == std::string::npos ? globals.end() : globals.find(key.substr(0, dot));
    if (global == globals.end()) return fail("unknown key " + key);
    std::string field = key.substr(dot + 1);
    GlobalRef& ref = *global->second;
    if (field == "signature") {
      if (!value.empty() && !parsePattern(value)) return fail("bad signature for " + key);
      ref.signature = value;
    } else if (field == "disp_offset") {
      if (!parseNumber(value, ref.dispOffset)) return fail("bad number for " + key);
    } else if (field == "instr_length") {
      if (!parseNumber(value, ref.instrLength)) return fail("bad number for " + key);
    } else {
      return fail("unknown key " + key);
    }
  }
  return true;
}

}  // namespace coop
