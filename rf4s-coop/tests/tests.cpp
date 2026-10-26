#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "coop/config.hpp"
#include "coop/protocol.hpp"
#include "coop/session.hpp"
#include "coop/signature.hpp"
#include "coop/sim_game.hpp"

using namespace coop;

namespace {

struct Test {
  const char* name;
  std::function<void()> body;
};
std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}
struct Register {
  Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};
int g_failures = 0;

#define TEST(name)                                     \
  static void name();                                  \
  static Register register_##name(#name, name);        \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

template <typename T>
std::optional<T> roundTrip(const T& msg) {
  auto bytes = encode(msg);
  auto decoded = decode(bytes.data(), bytes.size());
  if (!decoded || !std::holds_alternative<T>(*decoded)) return std::nullopt;
  return std::get<T>(*decoded);
}

float distance(const Vec3& a, const Vec3& b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Steps a host and a guest through simulated time over real loopback UDP.
struct Pair {
  SimGame hostGame{1, 10.f, 1.f};
  SimGame guestGame{1, 3.f, -1.f};
  std::unique_ptr<Session> host;
  std::unique_ptr<GuestSession> guest;
  double t = 0;

  explicit Pair(uint16_t port, uint32_t guestBuild = 7) {
    SessionConfig hc;
    hc.port = port;
    hc.gameBuild = 7;
    host = createHost(hostGame, hc);
    SessionConfig gc;
    gc.port = port;
    gc.hostAddress = "127.0.0.1";
    gc.gameBuild = guestBuild;
    guest = createGuest(guestGame, gc);
  }

  void run(double seconds, bool hostRuns = true) {
    const double dt = 1.0 / 60;
    for (double end = t + seconds; t < end; t += dt) {
      hostGame.advance(dt);
      guestGame.advance(dt);
      if (hostRuns) host->update(t);
      guest->update(t);
    }
  }
};

// ------------------------------------------------------------------ protocol

TEST(protocol_round_trips_every_message) {
  auto hello = roundTrip(Hello{3, 0xDEADBEEF, "Frey"});
  CHECK(hello && hello->modVersion == 3 && hello->gameBuild == 0xDEADBEEF && hello->name == "Frey");

  auto welcome = roundTrip(Welcome{false, 0, "Session is full"});
  CHECK(welcome && !welcome->accepted && welcome->reason == "Session is full");

  PlayerState state;
  state.tick = 123456;
  state.body = {42, {1.5f, -2.25f, 300.f}, 3.1f, 17, 950, 1000, 80, 120};
  auto s = roundTrip(state);
  CHECK(s && s->tick == 123456 && s->body.mapId == 42 && s->body.pos.y == -2.25f && s->body.facing == 3.1f &&
        s->body.anim == 17 && s->body.hp == 950 && s->body.maxRp == 120);

  auto clock = roundTrip(ClockSync{{2, 3, 30, 23, 59}});
  CHECK(clock && clock->clock == (GameClock{2, 3, 30, 23, 59}));

  auto chat = roundTrip(Chat{"Meet at the dragon"});
  CHECK(chat && chat->text == "Meet at the dragon");

  auto bye = roundTrip(Goodbye{GoodbyeReason::Timeout});
  CHECK(bye && bye->reason == GoodbyeReason::Timeout);
}

TEST(protocol_rejects_malformed_packets) {
  auto bytes = encode(Chat{"hi"});
  CHECK(decode(bytes.data(), bytes.size()));

  auto wrongVersion = bytes;
  wrongVersion[0] = kProtocolVersion + 1;
  CHECK(!decode(wrongVersion.data(), wrongVersion.size()));

  CHECK(!decode(bytes.data(), bytes.size() - 1));  // truncated
  auto trailing = bytes;
  trailing.push_back(0);
  CHECK(!decode(trailing.data(), trailing.size()));

  auto unknownType = bytes;
  unknownType[1] = 0x77;
  CHECK(!decode(unknownType.data(), unknownType.size()));
  CHECK(!decode(nullptr, 0));

  PlayerState bad;
  bad.body.pos.x = std::numeric_limits<float>::quiet_NaN();
  auto nan = encode(bad);
  CHECK(!decode(nan.data(), nan.size()));

  auto badClock = encode(ClockSync{{1, 0, 1, 25, 0}});
  CHECK(!decode(badClock.data(), badClock.size()));
}

TEST(protocol_truncates_long_strings) {
  auto chat = roundTrip(Chat{std::string(1000, 'x')});
  CHECK(chat && chat->text.size() == kMaxString);
}

// ---------------------------------------------------------------- signatures

TEST(signature_parses_and_finds) {
  CHECK(!parsePattern(""));
  CHECK(!parsePattern("?? ??"));
  CHECK(!parsePattern("4G"));
  CHECK(!parsePattern("123"));

  auto p = parsePattern("48 8B ?? 05");
  CHECK(p && p->bytes.size() == 4 && p->bytes[2] == -1);

  std::vector<uint8_t> data = {0x00, 0x48, 0x8B, 0x11, 0x05, 0x48, 0x8B, 0x22, 0x05, 0x48};
  auto hits = findAll(data.data(), data.size(), *p);
  CHECK(hits.size() == 2 && hits[0] == 1 && hits[1] == 5);
  CHECK(!findUnique(data.data(), data.size(), *p));

  auto unique = parsePattern("48 8B 22");
  CHECK(unique && findUnique(data.data(), data.size(), *unique) == 5u);
  auto tooLong = parsePattern("00 48 8B 11 05 48 8B 22 05 48 00");
  CHECK(tooLong && findAll(data.data(), data.size(), *tooLong).empty());
}

TEST(signature_follows_rip_relative) {
  // mov rax, [rip+0x10] at offset 4: 48 8B 05 10 00 00 00 -> 4 + 7 + 0x10
  std::vector<uint8_t> data(64, 0);
  const uint8_t instr[] = {0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00};
  std::memcpy(data.data() + 4, instr, sizeof instr);
  CHECK(resolveRipRelative(data.data(), data.size(), 4, 3, 7) == 27u);

  // A negative displacement pointing before the start is rejected.
  const uint8_t back[] = {0xF0, 0xFF, 0xFF, 0xFF};
  std::memcpy(data.data() + 7, back, sizeof back);
  CHECK(!resolveRipRelative(data.data(), data.size(), 4, 3, 7));
  CHECK(!resolveRipRelative(data.data(), data.size(), 60, 3, 7));  // runs off the end
}

// -------------------------------------------------------------------- config

TEST(config_parses_ini) {
  std::string error;
  auto c = parseConfig("# comment\nrole = guest\nhost_address = 192.168.1.20\nport=9000\nname = Lest\n", error);
  CHECK(c && c->role == Role::Guest && c->session.hostAddress == "192.168.1.20" && c->session.port == 9000 &&
        c->session.playerName == "Lest");

  CHECK(!parseConfig("role = guest\n", error));  // guest needs an address
  CHECK(!parseConfig("role = spectator\n", error));
  CHECK(!parseConfig("port = 70000\n", error));
  CHECK(!parseConfig("just words\n", error));
  auto defaults = parseConfig("", error);
  CHECK(defaults && defaults->role == Role::Host && defaults->session.port == 7777);
}

// ------------------------------------------------------------- interpolation

TEST(remote_player_interpolates_between_snapshots) {
  RemotePlayer r(20, 2);
  CHECK(!r.view(0));

  PlayerState a{10, {1, {0, 0, 0}, 0}};
  PlayerState b{12, {1, {10, 0, 20}, 0}};
  r.onState(b, 0.0);
  r.onState(a, 0.0);  // out of order is fine
  auto mid = r.sample(11);
  CHECK(mid && std::abs(mid->pos.x - 5) < 1e-4 && std::abs(mid->pos.z - 10) < 1e-4);
  CHECK(r.sample(50)->pos.x == 10);  // never extrapolates
  CHECK(r.sample(0)->pos.x == 0);
}

TEST(remote_player_turns_the_short_way) {
  RemotePlayer r(20, 0);
  r.onState({0, {1, {}, 3.0f}}, 0);
  r.onState({2, {1, {}, -3.0f}}, 0);
  auto mid = r.sample(1);
  // From 3.0 to -3.0 the short way crosses pi, not zero.
  CHECK(mid && std::abs(std::abs(mid->facing) - 3.1416f) < 0.01f);
}

TEST(remote_player_snaps_across_map_change_and_restart) {
  RemotePlayer r(20, 0);
  r.onState({0, {1, {0, 0, 0}, 0}}, 0);
  r.onState({2, {2, {100, 0, 0}, 0}}, 0);
  CHECK(r.sample(1)->mapId == 2 && r.sample(1)->pos.x == 100);

  r.onState({500, {2, {1, 0, 0}, 0}}, 1);
  r.onState({3, {2, {7, 0, 0}, 0}}, 2);  // sender restarted its tick count
  auto v = r.view(2);
  CHECK(v && v->pos.x == 7);
}

// ------------------------------------------------------------------ sessions

TEST(guest_joins_and_follows_host_smoothly) {
  Pair p(47711);
  CHECK(p.host && p.guest);
  p.run(5);
  CHECK(p.host->connected() && p.guest->connected());

  // Each side sees the other about 0.1 s in the past (2 ticks at 20/s).
  auto seenByGuest = p.guestGame.remote();
  CHECK(seenByGuest && distance(seenByGuest->pos, p.hostGame.positionAt(p.t - 0.1)) < 0.5f);
  auto seenByHost = p.hostGame.remote();
  CHECK(seenByHost && distance(seenByHost->pos, p.guestGame.positionAt(p.t - 0.1)) < 0.5f);
}

TEST(guest_clock_follows_host) {
  Pair p(47712);
  p.hostGame.writeClock({1, 2, 15, 18, 30});
  p.run(3);
  auto host = p.hostGame.readClock();
  auto guest = p.guestGame.readClock();
  // Clock packets go out once a second; the guest may be up to a minute behind.
  CHECK(guest.day == 15 && guest.season == 2 && host.hour == guest.hour &&
        (host.minute - guest.minute == 0 || host.minute - guest.minute == 1));
}

TEST(host_rejects_a_different_game_build) {
  Pair p(47713, 8);
  p.run(2);
  CHECK(p.guest->failed() && !p.guest->connected() && !p.host->connected());
}

TEST(session_holds_only_one_guest) {
  Pair p(47714);
  SimGame thirdGame(1, 1.f, 1.f);
  SessionConfig c;
  c.port = 47714;
  c.hostAddress = "127.0.0.1";
  c.gameBuild = 7;
  auto third = createGuest(thirdGame, c);
  const double dt = 1.0 / 60;
  for (int i = 0; i < 120; ++i, p.t += dt) {
    p.host->update(p.t);
    p.guest->update(p.t);
    third->update(p.t);
  }
  CHECK(p.guest->connected());
  CHECK(third->failed());
}

TEST(guest_is_leashed_to_the_host_map) {
  Pair p(47715);
  p.guestGame.warpLocalPlayer(9, {});  // guest wanders off to another map
  p.run(5);
  CHECK(p.guestGame.warps() == 1);  // not yet: leash waits 10 s
  CHECK(!p.guestGame.remote());     // partner hidden while apart
  p.run(6);
  CHECK(p.guestGame.warps() == 2 && p.guestGame.readLocalPlayer().mapId == 1);
  p.run(1);
  CHECK(p.guestGame.remote().has_value());
}

TEST(leaving_and_timeouts_drop_the_partner) {
  Pair p(47716);
  p.run(2);
  p.guest->leave();
  p.run(0.5);
  CHECK(!p.host->connected() && !p.hostGame.remote());

  Pair q(47717);
  q.run(2);
  q.run(6, /*hostRuns=*/false);  // host freezes
  CHECK(!q.guest->connected() && !q.guestGame.remote() && !q.guest->failed());
  q.run(2);  // host comes back; guest rejoins
  CHECK(q.guest->connected());
}

}  // namespace

int main() {
  int failedTests = 0;
  for (auto& test : registry()) {
    int before = g_failures;
    test.body();
    bool ok = g_failures == before;
    failedTests += ok ? 0 : 1;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.name);
  }
  std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
  return failedTests == 0 ? 0 : 1;
}
