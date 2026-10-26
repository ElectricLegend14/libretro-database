#include "coop/storage.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace coop {
namespace {

namespace fs = std::filesystem;

constexpr char kMagic[8] = {'R', 'F', '4', 'C', 'O', 'O', 'P', 'C'};
constexpr uint16_t kFormatVersion = 1;
constexpr size_t kMaxItems = 4096;

void put(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

class Cursor {
 public:
  Cursor(const std::vector<uint8_t>& data, size_t end) : data_(data), end_(end) {}
  template <typename T>
  bool get(T& out) {
    if (end_ - pos_ < sizeof(T)) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
    pos_ += sizeof(T);
    out = static_cast<T>(v);
    return true;
  }
  bool str(std::string& out) {
    uint16_t n;
    if (!get(n) || end_ - pos_ < n) return false;
    out.assign(reinterpret_cast<const char*>(data_.data() + pos_), n);
    pos_ += n;
    return true;
  }
  bool done() const { return pos_ == end_; }
  void skip(size_t n) { pos_ += n; }

 private:
  const std::vector<uint8_t>& data_;
  size_t end_;
  size_t pos_ = 0;
};

std::optional<std::vector<uint8_t>> readFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path withSuffix(const fs::path& file, const char* suffix) {
  fs::path p = file;
  p += suffix;
  return p;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// Layout: magic[8] version u16 | fields... | crc32 of everything before it.
std::vector<uint8_t> serializeCharacter(const GuestCharacter& c) {
  std::vector<uint8_t> out(std::begin(kMagic), std::end(kMagic));
  put(out, kFormatVersion, 2);
  size_t nameLen = std::min<size_t>(c.name.size(), 64);
  put(out, nameLen, 2);
  out.insert(out.end(), c.name.begin(), c.name.begin() + static_cast<std::ptrdiff_t>(nameLen));
  put(out, c.level, 2);
  put(out, c.hp, 4);
  put(out, c.maxHp, 4);
  put(out, c.rp, 4);
  put(out, c.maxRp, 4);
  put(out, c.gold, 8);
  size_t items = std::min(c.items.size(), kMaxItems);
  put(out, items, 2);
  for (size_t i = 0; i < items; ++i) {
    put(out, c.items[i].itemId, 2);
    put(out, c.items[i].count, 2);
  }
  put(out, c.playSeconds, 4);
  put(out, crc32(out.data(), out.size()), 4);
  return out;
}

std::optional<GuestCharacter> parseCharacter(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < sizeof kMagic + 2 + 4 || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
    return std::nullopt;
  }
  size_t body = bytes.size() - 4;
  Cursor crcAt(bytes, bytes.size());
  crcAt.skip(body);
  uint32_t stored;
  if (!crcAt.get(stored) || stored != crc32(bytes.data(), body)) return std::nullopt;

  Cursor r(bytes, body);
  r.skip(sizeof kMagic);
  uint16_t version, items;
  GuestCharacter c;
  if (!r.get(version) || version != kFormatVersion || !r.str(c.name) || !r.get(c.level) || !r.get(c.hp) ||
      !r.get(c.maxHp) || !r.get(c.rp) || !r.get(c.maxRp) || !r.get(c.gold) || !r.get(items) || items > kMaxItems) {
    return std::nullopt;
  }
  c.items.resize(items);
  for (auto& stack : c.items) {
    if (!r.get(stack.itemId) || !r.get(stack.count)) return std::nullopt;
  }
  if (!r.get(c.playSeconds) || !r.done()) return std::nullopt;
  return c;
}

bool saveCharacter(const fs::path& file, const GuestCharacter& c, std::string& error) {
  auto bytes = serializeCharacter(c);
  fs::path tmp = withSuffix(file, ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      error = "couldn't write " + tmp.string();
      return false;
    }
  }
  std::error_code ec;
  if (fs::exists(file, ec)) fs::copy_file(file, withSuffix(file, ".bak"), fs::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "couldn't keep a backup: " + ec.message();
    return false;
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    error = "couldn't replace " + file.string() + ": " + ec.message();
    return false;
  }
  return true;
}

std::optional<GuestCharacter> loadCharacter(const fs::path& file, std::string& error) {
  for (const fs::path& candidate : {file, withSuffix(file, ".bak")}) {
    auto bytes = readFile(candidate);
    if (!bytes) continue;
    if (auto c = parseCharacter(*bytes)) return c;
  }
  error = "no readable character in " + file.string() + " or its .bak";
  return std::nullopt;
}

std::optional<fs::path> backupSaves(const fs::path& saveDir, const fs::path& backupRoot, const std::string& stamp,
                                    size_t keep, std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(saveDir, ec)) {
    error = "save folder not found: " + saveDir.string();
    return std::nullopt;
  }
  // Let copy() create the target itself: some standard libraries (MinGW's)
  // refuse to copy a folder into one that already exists.
  fs::path target = backupRoot / stamp;
  fs::create_directories(backupRoot, ec);
  if (!ec) fs::remove_all(target, ec);
  if (!ec) fs::copy(saveDir, target, fs::copy_options::recursive, ec);
  if (ec) {
    error = "backup failed: " + ec.message();
    return std::nullopt;
  }

  // Stamps sort by time, so the oldest come first.
  std::vector<fs::path> backups;
  for (auto& entry : fs::directory_iterator(backupRoot, ec)) {
    if (entry.is_directory()) backups.push_back(entry.path());
  }
  std::sort(backups.begin(), backups.end());
  for (size_t i = 0; i + keep < backups.size(); ++i) fs::remove_all(backups[i], ec);
  return target;
}

}  // namespace coop
