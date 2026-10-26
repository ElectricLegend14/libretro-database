#include "coop/nso.hpp"

#include <cstring>

namespace coop {

namespace {

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// NSO header layout (switchbrew.org/wiki/NSO).
constexpr size_t kHeaderSize = 0x100;
constexpr size_t kFlags = 0x0C;
constexpr size_t kTextSegment = 0x10;  // file offset, memory offset, decompressed size
constexpr size_t kBuildId = 0x40;
constexpr size_t kTextFileSize = 0x60;
constexpr uint32_t kTextCompressed = 1u << 0;

}  // namespace

bool lz4DecompressBlock(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    size_t in = 0, out = 0;
    while (in < srcSize) {
        uint8_t token = src[in++];

        size_t literals = token >> 4;
        if (literals == 15) {
            uint8_t b;
            do {
                if (in >= srcSize) return false;
                b = src[in++];
                literals += b;
            } while (b == 255);
        }
        if (literals > srcSize - in || literals > dstSize - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;

        if (in == srcSize) break;  // the last sequence has literals only

        if (srcSize - in < 2) return false;
        size_t distance = size_t(src[in]) | size_t(src[in + 1]) << 8;
        in += 2;
        if (distance == 0 || distance > out) return false;

        size_t length = (token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            uint8_t b;
            do {
                if (in >= srcSize) return false;
                b = src[in++];
                length += b;
            } while (b == 255);
        }
        if (length > dstSize - out) return false;
        // Byte by byte: matches may overlap the bytes they produce.
        for (size_t i = 0; i < length; ++i, ++out) dst[out] = dst[out - distance];
    }
    return out == dstSize;
}

bool loadNsoText(const uint8_t* data, size_t size, NsoText& out, std::string& error) {
    if (size < kHeaderSize || std::memcmp(data, "NSO0", 4) != 0) {
        error = "not an NSO file (expected the game's exefs 'main')";
        return false;
    }
    uint32_t flags = readU32(data + kFlags);
    uint32_t fileOffset = readU32(data + kTextSegment);
    uint32_t memOffset = readU32(data + kTextSegment + 4);
    uint32_t textSize = readU32(data + kTextSegment + 8);
    uint32_t storedSize = (flags & kTextCompressed) ? readU32(data + kTextFileSize) : textSize;

    if (fileOffset > size || storedSize > size - fileOffset) {
        error = "NSO is truncated";
        return false;
    }

    out.code.resize(textSize);
    if (flags & kTextCompressed) {
        if (!lz4DecompressBlock(data + fileOffset, storedSize, out.code.data(), textSize)) {
            error = "couldn't decompress the NSO's code";
            return false;
        }
    } else {
        std::memcpy(out.code.data(), data + fileOffset, textSize);
    }
    out.textOffset = memOffset;
    std::memcpy(out.buildId.data(), data + kBuildId, out.buildId.size());
    return true;
}

}  // namespace coop
