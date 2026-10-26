#include "coop/signature.hpp"

#include <cstring>

namespace coop {
namespace {

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool matchesAt(const uint8_t* data, const Pattern& pattern) {
  for (size_t i = 0; i < pattern.bytes.size(); ++i) {
    if (pattern.bytes[i] >= 0 && data[i] != pattern.bytes[i]) return false;
  }
  return true;
}

}  // namespace

std::optional<Pattern> parsePattern(std::string_view text) {
  Pattern pattern;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      ++i;
      continue;
    }
    size_t end = text.find(' ', i);
    std::string_view token = text.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
    i += token.size();

    if (token == "?" || token == "??") {
      pattern.bytes.push_back(-1);
    } else if (token.size() == 2 && hexValue(token[0]) >= 0 && hexValue(token[1]) >= 0) {
      pattern.bytes.push_back(static_cast<int16_t>(hexValue(token[0]) * 16 + hexValue(token[1])));
    } else {
      return std::nullopt;
    }
  }
  // A pattern must anchor on at least one real byte.
  for (auto b : pattern.bytes) {
    if (b >= 0) return pattern;
  }
  return std::nullopt;
}

std::vector<size_t> findAll(const uint8_t* data, size_t len, const Pattern& pattern) {
  std::vector<size_t> hits;
  size_t n = pattern.bytes.size();
  if (n == 0 || n > len) return hits;
  for (size_t at = 0; at + n <= len; ++at) {
    if (matchesAt(data + at, pattern)) hits.push_back(at);
  }
  return hits;
}

std::optional<size_t> findUnique(const uint8_t* data, size_t len, const Pattern& pattern) {
  auto hits = findAll(data, len, pattern);
  if (hits.size() != 1) return std::nullopt;
  return hits.front();
}

std::optional<size_t> resolveRipRelative(const uint8_t* data, size_t len, size_t at, size_t dispOffset,
                                         size_t length) {
  if (dispOffset + 4 > length || at > len || length > len - at) return std::nullopt;
  int32_t disp;
  std::memcpy(&disp, data + at + dispOffset, sizeof disp);
  auto target = static_cast<int64_t>(at + length) + disp;
  if (target < 0 || static_cast<uint64_t>(target) >= len) return std::nullopt;
  return static_cast<size_t>(target);
}

}  // namespace coop
