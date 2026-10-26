#include "coop/pe.hpp"

#include <algorithm>
#include <cstring>

#include "coop/signature.hpp"

namespace coop {
namespace {

template <typename T>
bool readAt(const std::vector<uint8_t>& file, size_t offset, T& out) {
  if (offset > file.size() || file.size() - offset < sizeof(T)) return false;
  std::memcpy(&out, file.data() + offset, sizeof(T));
  return true;
}

// The bytes of a section as stored in the file (the tail that's only
// zero-filled in memory isn't there).
std::pair<const uint8_t*, size_t> rawBytes(const std::vector<uint8_t>& file, const PeSection& s) {
  size_t size = std::min<size_t>(s.rawSize, s.virtualSize ? s.virtualSize : s.rawSize);
  if (s.rawOffset > file.size()) return {nullptr, 0};
  size = std::min(size, file.size() - s.rawOffset);
  return {file.data() + s.rawOffset, size};
}

}  // namespace

const PeSection* PeImage::codeSection() const {
  const PeSection* best = nullptr;
  for (auto& s : sections) {
    if (s.executable && (!best || s.virtualSize > best->virtualSize)) best = &s;
  }
  return best;
}

const PeSection* PeImage::sectionAt(uint32_t rva) const {
  for (auto& s : sections) {
    uint32_t size = std::max(s.virtualSize, s.rawSize);
    if (rva >= s.rva && rva - s.rva < size) return &s;
  }
  return nullptr;
}

std::optional<PeImage> parsePe(const std::vector<uint8_t>& file, std::string& error) {
  uint16_t mz;
  uint32_t peOffset, signature;
  if (!readAt(file, 0, mz) || mz != 0x5A4D || !readAt(file, 0x3C, peOffset) || !readAt(file, peOffset, signature) ||
      signature != 0x00004550) {
    error = "not a Windows executable";
    return std::nullopt;
  }
  size_t fileHeader = peOffset + 4;
  uint16_t machine, sectionCount, optionalSize, magic;
  PeImage pe;
  if (!readAt(file, fileHeader, machine) || !readAt(file, fileHeader + 2, sectionCount) ||
      !readAt(file, fileHeader + 4, pe.timestamp) || !readAt(file, fileHeader + 16, optionalSize) ||
      !readAt(file, fileHeader + 20, magic)) {
    error = "truncated PE header";
    return std::nullopt;
  }
  if (machine != 0x8664 || magic != 0x20B) {
    error = "not a 64-bit (x64) executable";
    return std::nullopt;
  }
  size_t table = fileHeader + 20 + optionalSize;
  for (uint16_t i = 0; i < sectionCount; ++i) {
    size_t at = table + i * 40u;
    char name[9] = {};
    uint32_t characteristics;
    PeSection s;
    if (at + 40 > file.size() || !readAt(file, at + 8, s.virtualSize) || !readAt(file, at + 12, s.rva) ||
        !readAt(file, at + 16, s.rawSize) || !readAt(file, at + 20, s.rawOffset) ||
        !readAt(file, at + 36, characteristics)) {
      error = "truncated section table";
      return std::nullopt;
    }
    std::memcpy(name, file.data() + at, 8);
    s.name = name;
    s.executable = characteristics & 0x20000000;  // IMAGE_SCN_MEM_EXECUTE
    pe.sections.push_back(s);
  }
  return pe;
}

std::vector<LayoutCheck> checkLayout(const PeImage& pe, const std::vector<uint8_t>& file, const GameLayout& layout) {
  std::vector<LayoutCheck> out;
  const PeSection* code = pe.codeSection();
  auto [bytes, size] = code ? rawBytes(file, *code) : std::pair<const uint8_t*, size_t>{nullptr, 0};

  auto find = [&](const std::string& name, const std::string& sig, LayoutCheck& c) -> std::optional<size_t> {
    c.name = name;
    c.hasSignature = !sig.empty();
    if (!c.hasSignature || !bytes) return std::nullopt;
    auto pattern = parsePattern(sig);
    auto hits = pattern ? findAll(bytes, size, *pattern) : std::vector<size_t>{};
    c.matches = hits.size();
    if (hits.size() != 1) return std::nullopt;
    c.matchRva = code->rva + static_cast<uint32_t>(hits.front());
    return hits.front();
  };
  auto locate = [&](LayoutCheck& c, int64_t rva) {
    if (rva < 0 || rva > UINT32_MAX) return;
    c.targetRva = static_cast<uint32_t>(rva);
    if (auto* s = pe.sectionAt(c.targetRva)) c.targetSection = s->name;
  };

  const std::pair<const char*, const GlobalRef*> globals[] = {
      {"player", &layout.player}, {"map", &layout.mapId}, {"clock", &layout.clock}};
  for (auto [name, ref] : globals) {
    LayoutCheck c;
    if (auto hit = find(name, ref->signature, c)) {
      int32_t disp;
      if (ref->dispOffset + 4 <= ref->instrLength && *hit + ref->dispOffset + 4 <= size) {
        std::memcpy(&disp, bytes + *hit + ref->dispOffset, 4);
        locate(c, int64_t{c.matchRva} + ref->instrLength + disp);
      }
    }
    out.push_back(c);
  }

  LayoutCheck frame;
  if (find("frame", layout.frame.signature, frame)) locate(frame, int64_t{frame.matchRva} + layout.frame.offset);
  out.push_back(frame);
  return out;
}

}  // namespace coop
