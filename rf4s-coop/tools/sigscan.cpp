// sigscan: checks a signature against a file, usually the game executable.
//
//   sigscan <file> "48 8B 05 ?? ?? ?? ??"
//
// Prints the file offset of every match. A signature is usable only when it
// matches exactly once; the exit code is 0 in that case and 1 otherwise.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "coop/signature.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: sigscan <file> \"<pattern>\"\n");
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "Can't open %s\n", argv[1]);
    return 2;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  auto pattern = coop::parsePattern(argv[2]);
  if (!pattern) {
    std::fprintf(stderr, "Bad pattern. Use hex bytes and ?? wildcards, separated by spaces.\n");
    return 2;
  }

  auto hits = coop::findAll(data.data(), data.size(), *pattern);
  for (size_t at : hits) std::printf("0x%zx\n", at);
  if (hits.size() == 1) {
    std::printf("Unique match: this signature is usable.\n");
    return 0;
  }
  std::printf("%zu matches: %s\n", hits.size(),
              hits.empty() ? "check the bytes" : "make the signature longer so it matches once");
  return 1;
}
