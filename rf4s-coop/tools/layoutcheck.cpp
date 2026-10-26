// layoutcheck: checks an rf4s-layout.ini against the game's executable on
// disk, without running the game.
//
//   layoutcheck "<game>.exe" rf4s-layout.ini
//
// For each signature it reports how often it matches (it must be exactly
// once) and where it points. Exit code 0 when every signature given is good.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "coop/pe.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: layoutcheck <game.exe> <rf4s-layout.ini>\n");
    return 2;
  }
  std::ifstream exe(argv[1], std::ios::binary);
  std::ifstream ini(argv[2]);
  if (!exe || !ini) {
    std::fprintf(stderr, "Can't open %s\n", !exe ? argv[1] : argv[2]);
    return 2;
  }
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(exe)), std::istreambuf_iterator<char>());
  std::stringstream text;
  text << ini.rdbuf();

  std::string error;
  auto pe = coop::parsePe(file, error);
  if (!pe) {
    std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
    return 2;
  }
  coop::GameLayout layout;
  if (!coop::applyLayoutOverrides(text.str(), layout, error)) {
    std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
    return 2;
  }

  std::printf("Game build %08x\n", pe->timestamp);
  if (auto* code = pe->codeSection()) {
    std::printf("Code section %s at RVA 0x%x, %u KB\n", code->name.c_str(), code->rva, code->virtualSize / 1024);
  }
  bool allOk = true;
  for (auto& c : coop::checkLayout(*pe, file, layout)) {
    allOk = allOk && c.ok();
    if (!c.hasSignature) {
      std::printf("  %-7s no signature\n", c.name.c_str());
    } else if (c.matches != 1) {
      std::printf("  %-7s BAD: matched %zu times, need exactly 1\n", c.name.c_str(), c.matches);
    } else if (c.targetSection.empty()) {
      std::printf("  %-7s BAD: match at RVA 0x%x points outside the executable (check disp_offset/instr_length)\n",
                  c.name.c_str(), c.matchRva);
    } else {
      std::printf("  %-7s ok: match at RVA 0x%x -> RVA 0x%x in %s\n", c.name.c_str(), c.matchRva, c.targetRva,
                  c.targetSection.c_str());
    }
  }
  return allOk ? 0 : 1;
}
