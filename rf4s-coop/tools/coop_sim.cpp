// coop_sim: runs a host or guest against a simulated game, so the network
// side can be tried on any OS without RF4S. The host's world has a few
// monsters near the guest's path; the guest swings at whatever is in reach
// and picks up what drops.
//
//   coop_sim host [port] [--seconds N] [--build HEX]
//   coop_sim guest <host address> [port] [--seconds N] [--build HEX]
//
// --build sets the game build to report. Use the one printed in
// rf4s-coop.log to join a host running inside the real game.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "coop/session.hpp"
#include "coop/sim_game.hpp"

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: coop_sim host [port] [--seconds N] [--build HEX]\n"
               "       coop_sim guest <host address> [port] [--seconds N] [--build HEX]\n");
  return 2;
}

// The guest's "player": swing at monsters in reach, grab drops in reach.
void autoplay(coop::SimGame& game) {
  auto self = game.readLocalPlayer();
  for (auto& [id, e] : game.mirrored()) {
    float d = coop::distance(e.pos, self.pos);
    if (e.category == coop::EntityCategory::Monster && d <= coop::SimGame::kMeleeRange) {
      game.queueAttack({coop::AttackKind::Melee, 1, self.pos, self.facing, id});
      std::printf("  swing at monster %u (HP %u)\n", id, e.hp);
      return;
    }
    if (e.category == coop::EntityCategory::Drop && d <= 3) {
      game.queuePickup(id);
      std::printf("  pick up item %u\n", e.kind);
      return;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  std::string role = argv[1];
  bool isHost = role == "host";
  if (!isHost && role != "guest") return usage();

  coop::SessionConfig config;
  config.playerName = isHost ? "Host" : "Guest";
  config.log = [](const std::string& line) { std::printf("[coop] %s\n", line.c_str()); };
  double seconds = 0;  // 0 = run until killed

  int positional = 0;
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      seconds = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--build") == 0 && i + 1 < argc) {
      config.gameBuild = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 16));
    } else if (!isHost && positional == 0) {
      config.hostAddress = argv[i];
      ++positional;
    } else {
      config.port = static_cast<uint16_t>(std::atoi(argv[i]));
      ++positional;
    }
  }
  if (!isHost && config.hostAddress.empty()) return usage();

  // Host walks a wide circle on map 1, guest a small one on map 1.
  coop::SimGame game(1, isHost ? 10.f : 3.f, isHost ? 0.8f : -1.5f);
  if (isHost) {
    game.spawnMonster(1, {3, 0, 1}, 40);
    game.spawnMonster(2, {-3, 0, 0}, 40);
    game.spawnMonster(3, {0, 0, 3}, 60);
  }
  std::unique_ptr<coop::Session> session;
  if (isHost) session = coop::createHost(game, config);
  else session = coop::createGuest(game, config);
  if (!session) return 1;

  using clock = std::chrono::steady_clock;
  auto start = clock::now();
  auto last = start;
  double nextPrint = 1;
  double nextSwing = 0;
  for (;;) {
    auto now = clock::now();
    double t = std::chrono::duration<double>(now - start).count();
    game.advance(std::chrono::duration<double>(now - last).count());
    last = now;
    session->update(t);
    if (!isHost && t >= nextSwing) {
      nextSwing = t + 0.5;
      autoplay(game);
    }

    if (t >= nextPrint) {
      nextPrint += 1;
      auto c = game.readClock();
      auto self = game.readLocalPlayer();
      if (auto r = game.remote()) {
        size_t monsters = isHost ? game.world().size() : game.mirrored().size();
        std::printf("t=%4.0fs  %02u:%02u  partner (%6.2f, %6.2f)  HP %3u  %s %zu  items %zu\n", t, c.hour, c.minute,
                    r->pos.x, r->pos.z, self.hp, isHost ? "world" : "seen", monsters, game.inventory().size());
      } else {
        std::printf("t=%4.0fs  %02u:%02u  waiting for partner\n", t, c.hour, c.minute);
      }
    }
    if (seconds > 0 && t >= seconds) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  session->leave();
  return 0;
}
