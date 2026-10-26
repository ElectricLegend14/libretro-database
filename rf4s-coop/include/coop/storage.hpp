#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "coop/types.hpp"

namespace coop {

std::vector<uint8_t> serializeCharacter(const GuestCharacter& c);
// Checks the magic, version and CRC before trusting anything.
std::optional<GuestCharacter> parseCharacter(const std::vector<uint8_t>& bytes);

// Writes to a temporary file first and swaps it in, keeping the previous
// version as `<file>.bak`, so a crash mid-write never loses the character.
bool saveCharacter(const std::filesystem::path& file, const GuestCharacter& c, std::string& error);

// Falls back to `<file>.bak` when the main file is missing or damaged.
std::optional<GuestCharacter> loadCharacter(const std::filesystem::path& file, std::string& error);

// Copies every file in `saveDir` into `backupRoot/<stamp>/`, then deletes the
// oldest backups beyond `keep`. Returns the new backup folder.
std::optional<std::filesystem::path> backupSaves(const std::filesystem::path& saveDir,
                                                 const std::filesystem::path& backupRoot, const std::string& stamp,
                                                 size_t keep, std::string& error);

uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace coop
