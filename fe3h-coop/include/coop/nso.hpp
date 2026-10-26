#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coop {

// The code segment of a Switch NSO executable (the game's exefs "main"), so
// coop_check can test offsets.ini patterns on your PC.
struct NsoText {
    std::vector<uint8_t> code;          // decompressed .text
    uint64_t textOffset = 0;            // where .text sits in the loaded module
    std::array<uint8_t, 32> buildId{};  // identifies the exact game version
};

// Returns false and sets `error` if this isn't a readable NSO.
bool loadNsoText(const uint8_t* data, size_t size, NsoText& out, std::string& error);

// LZ4 block decompression (the format NSO segments use). Returns false on
// corrupt input or if the output isn't exactly `dstSize` bytes.
bool lz4DecompressBlock(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);

}  // namespace coop
