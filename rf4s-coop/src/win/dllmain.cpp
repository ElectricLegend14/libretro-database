// rf4s-coop.dll: injected into Rune Factory 4 Special by rf4s-coop-inject.exe.
//
// Starts a background thread that finds the game's data, then runs a co-op
// session if rf4s-coop.ini asks for one. Until every signature in layout.hpp is
// known it stays in diagnostics mode and only logs what it can see, so it's
// safe to load at any stage of reverse engineering.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

#include "coop/config.hpp"
#include "coop/session.hpp"
#include "rf4s_adapter.hpp"

namespace {

std::atomic<bool> g_stop{false};
HMODULE g_self = nullptr;

std::filesystem::path modDirectory() {
  wchar_t path[MAX_PATH];
  DWORD len = GetModuleFileNameW(g_self, path, MAX_PATH);
  return std::filesystem::path(std::wstring(path, len)).parent_path();
}

class FileLog {
 public:
  explicit FileLog(const std::filesystem::path& path) : out_(path, std::ios::app) {}

  void operator()(const std::string& line) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[16];
    std::snprintf(stamp, sizeof stamp, "%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);
    std::lock_guard lock(mutex_);
    out_ << stamp << line << std::endl;
  }

 private:
  std::mutex mutex_;
  std::ofstream out_;
};

struct GameImage {
  const uint8_t* base;
  size_t size;
  size_t codeStart;
  size_t codeSize;
  uint32_t build;  // PE link timestamp: identical executables, identical build
};

std::optional<GameImage> inspectGame() {
  auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
  auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;

  GameImage image{base, nt->OptionalHeader.SizeOfImage, 0, 0, nt->FileHeader.TimeDateStamp};
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) && section->Misc.VirtualSize > image.codeSize) {
      image.codeStart = section->VirtualAddress;
      image.codeSize = section->Misc.VirtualSize;
    }
  }
  if (image.codeSize == 0) return std::nullopt;
  return image;
}

std::optional<coop::ModConfig> loadConfig(const std::filesystem::path& path, FileLog& log) {
  std::ifstream in(path);
  if (!in) {
    log("No rf4s-coop.ini next to the DLL, so co-op stays off");
    return std::nullopt;
  }
  std::stringstream text;
  text << in.rdbuf();
  std::string error;
  auto config = coop::parseConfig(text.str(), error);
  if (!config) log("rf4s-coop.ini: " + error);
  return config;
}

DWORD WINAPI run(LPVOID) {
  auto dir = modDirectory();
  FileLog log(dir / "rf4s-coop.log");
  auto logFn = [&log](const std::string& line) { log(line); };
  log("rf4s-coop " + std::to_string(coop::kModVersion) + " loaded");

  auto image = inspectGame();
  if (!image) {
    log("Couldn't read the game's executable headers");
    return 0;
  }
  char build[64];
  std::snprintf(build, sizeof build, "Game build %08x, code section %zu KB", image->build, image->codeSize / 1024);
  log(build);

  coop::Rf4sAdapter game(logFn);
  bool ready = game.resolve(image->base, image->size, image->codeStart, image->codeSize);
  auto config = loadConfig(dir / "rf4s-coop.ini", log);

  std::unique_ptr<coop::Session> session;
  if (!ready) {
    log("Some signatures or offsets are still unknown (src/win/layout.hpp), so co-op stays off");
  } else if (config) {
    config->session.gameBuild = image->build;
    config->session.log = logFn;
    if (config->role == coop::Role::Host) session = coop::createHost(game, config->session);
    else session = coop::createGuest(game, config->session);
  }

  // Runs beside the game's own loop rather than inside it. Good enough for
  // reading state; phase 2 moves this into a hook on the game's frame update.
  auto start = std::chrono::steady_clock::now();
  int frame = 0;
  while (!g_stop) {
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (session) {
      session->update(now);
    } else if (game.hasPlayer() && ++frame % 60 == 0) {
      // Phase 0 gate: the live player position shows up in the log.
      auto p = game.readLocalPlayer();
      char line[128];
      std::snprintf(line, sizeof line, "Player on map %u at (%.1f, %.1f, %.1f), HP %u/%u", p.mapId, p.pos.x, p.pos.y,
                    p.pos.z, p.hp, p.maxHp);
      log(line);
    }
    Sleep(16);
  }
  if (session) session->leave();
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = instance;
    DisableThreadLibraryCalls(instance);
    if (HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr)) CloseHandle(thread);
  } else if (reason == DLL_PROCESS_DETACH) {
    g_stop = true;
  }
  return TRUE;
}
