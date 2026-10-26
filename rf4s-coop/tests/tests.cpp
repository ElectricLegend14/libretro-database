#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "coop/config.hpp"
#include "coop/connection.hpp"
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
  auto bytes = encodeMessage(msg);
  auto decoded = decodeMessage(bytes.data(), bytes.size());
  if (!decoded || !std::holds_alternative<T>(*decoded)) return std::nullopt;
  return std::get<T>(*decoded);
}

float distance(const Vec3& a, const Vec3& b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// A perfect in-memory network: datagrams arrive instantly and in order.
// Wrap ports in ConditionedTransport to make it worse.
class MemoryNet {
 public:
  static constexpr uint32_t kIp = 0x7F000001;

  class Port : public ITransport {
   public:
    Port(MemoryNet& net, uint16_t port) : net_(net), self_{kIp, port} {}
    bool sendTo(const Endpoint& to, const std::vector<uint8_t>& data) override {
      net_.inbox_[to.port].push_back({self_, data});
      return true;
    }
    std::optional<Datagram> receive() override {
      auto& q = net_.inbox_[self_.port];
      if (q.empty()) return std::nullopt;
      Datagram d = std::move(q.front());
      q.pop_front();
      return d;
    }
    uint16_t localPort() const override { return self_.port; }

   private:
    MemoryNet& net_;
    Endpoint self_;
  };

  std::unique_ptr<ITransport> open(uint16_t port, NetworkConditions cond = {}) {
    inbox_[port].clear();
    return std::make_unique<ConditionedTransport>(std::make_unique<Port>(*this, port), cond);
  }
  static Endpoint at(uint16_t port) { return {kIp, port}; }

 private:
  std::map<uint16_t, std::deque<Datagram>> inbox_;
};

bool contains(const std::vector<std::string>& log, const std::string& line) {
  for (auto& l : log) {
    if (l == line) return true;
  }
  return false;
}

// Steps a host and a guest through simulated time.
struct Pair {
  static constexpr uint16_t kHostPort = 7777, kGuestPort = 5000;

  MemoryNet net;
  SimGame hostGame{1, 10.f, 1.f};
  SimGame guestGame{1, 3.f, -1.f};
  std::vector<std::string> hostLog, guestLog;
  std::unique_ptr<Session> host;
  std::unique_ptr<GuestSession> guest;
  double t = 0;

  explicit Pair(NetworkConditions cond = {}, uint32_t guestBuild = 7) {
    host = createHost(hostGame, config(7, hostLog, "Host"), net.open(kHostPort, cond));
    cond.seed += 1;
    guest = createGuest(guestGame, config(guestBuild, guestLog, "Guest"), net.open(kGuestPort, cond), MemoryNet::at(kHostPort));
  }

  SessionConfig config(uint32_t build, std::vector<std::string>& log, const std::string& name) {
    SessionConfig c;
    c.gameBuild = build;
    c.playerName = name;
    c.log = [&log](const std::string& line) { log.push_back(line); };
    return c;
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
  auto hello = roundTrip(Hello{3, 0xDEADBEEF, 99, "Frey"});
  CHECK(hello && hello->modVersion == 3 && hello->gameBuild == 0xDEADBEEF && hello->nonce == 99 &&
        hello->name == "Frey");

  auto welcome = roundTrip(Welcome{false, 0, 99, "Session is full"});
  CHECK(welcome && !welcome->accepted && welcome->nonce == 99 && welcome->reason == "Session is full");

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

TEST(protocol_rejects_malformed_messages) {
  auto bytes = encodeMessage(Chat{"hi"});
  CHECK(decodeMessage(bytes.data(), bytes.size()));
  CHECK(!decodeMessage(bytes.data(), bytes.size() - 1));  // truncated
  auto trailing = bytes;
  trailing.push_back(0);
  CHECK(!decodeMessage(trailing.data(), trailing.size()));

  auto unknownType = bytes;
  unknownType[0] = 0x77;
  CHECK(!decodeMessage(unknownType.data(), unknownType.size()));
  CHECK(!decodeMessage(nullptr, 0));

  PlayerState bad;
  bad.body.pos.x = std::numeric_limits<float>::quiet_NaN();
  auto nan = encodeMessage(bad);
  CHECK(!decodeMessage(nan.data(), nan.size()));

  auto badClock = encodeMessage(ClockSync{{1, 0, 1, 25, 0}});
  CHECK(!decodeMessage(badClock.data(), badClock.size()));
}

TEST(protocol_frames_packets) {
  auto body = encodeMessage(Chat{"hi"});
  auto packet = makePacket(PacketKind::Reliable, 0xBEEF, body);
  auto view = parsePacket(packet.data(), packet.size());
  CHECK(view && view->kind == PacketKind::Reliable && view->seq == 0xBEEF && view->bodyLen == body.size());

  auto wrongVersion = packet;
  wrongVersion[0] = kProtocolVersion + 1;
  CHECK(!parsePacket(wrongVersion.data(), wrongVersion.size()));
  auto badKind = packet;
  badKind[1] = 9;
  CHECK(!parsePacket(badKind.data(), badKind.size()));
  CHECK(!parsePacket(packet.data(), 3));

  auto ack = makePacket(PacketKind::Ack, 5, {});
  CHECK(parsePacket(ack.data(), ack.size()));
  auto ackWithBody = makePacket(PacketKind::Ack, 5, body);
  CHECK(!parsePacket(ackWithBody.data(), ackWithBody.size()));
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

// ---------------------------------------------------------------- connection

// Two connections joined by a (possibly bad) in-memory network.
struct Link {
  MemoryNet net;
  std::unique_ptr<ITransport> ta, tb;
  Connection a, b;
  std::vector<Message> gotA, gotB;
  double t = 0;

  explicit Link(NetworkConditions cond = {})
      : ta(net.open(1, cond)),
        tb(net.open(2, NetworkConditions{cond.latencySec, cond.jitterSec, cond.lossRate, cond.duplicateRate,
                                         cond.seed + 1})),
        a(*ta, MemoryNet::at(2)),
        b(*tb, MemoryNet::at(1)) {}

  void pump(ITransport& transport, Connection& conn, std::vector<Message>& got) {
    transport.advance(t);
    while (auto d = transport.receive()) {
      if (auto p = parsePacket(d->data.data(), d->data.size())) {
        for (auto& m : conn.onPacket(*p)) got.push_back(std::move(m));
      }
    }
    conn.update(t);
  }

  void run(double seconds) {
    for (double end = t + seconds; t < end; t += 1.0 / 60) {
      pump(*ta, a, gotA);
      pump(*tb, b, gotB);
    }
  }
};

std::vector<std::string> chats(const std::vector<Message>& msgs) {
  std::vector<std::string> out;
  for (auto& m : msgs) {
    if (auto* c = std::get_if<Chat>(&m)) out.push_back(c->text);
  }
  return out;
}

TEST(connection_delivers_reliable_in_order_on_a_bad_network) {
  Link link({0.05, 0.08, 0.3, 0.2, 42});  // 50-130 ms, 30% loss, 20% duplicates
  std::vector<std::string> sent;
  for (int i = 0; i < 300; ++i) {
    sent.push_back("msg " + std::to_string(i));
    link.a.sendReliable(Chat{sent.back()});
    if (i % 10 == 0) link.run(0.05);  // send in bursts while packets are in flight
  }
  link.run(5);
  CHECK(chats(link.gotB) == sent);
  CHECK(link.a.unackedCount() == 0);
}

TEST(connection_survives_sequence_wraparound) {
  Link link;
  std::vector<std::string> sent;
  for (int i = 0; i < 70000; ++i) {
    sent.push_back(std::to_string(i));
    link.a.sendReliable(Chat{sent.back()});
    if (i % 500 == 0) link.run(1.0 / 60);
  }
  link.run(0.5);
  CHECK(chats(link.gotB) == sent);
}

TEST(connection_skips_an_undecodable_reliable_message) {
  Link link;
  // seq 0 holds garbage, seq 1 a real message; seq 1 must not wait forever.
  link.ta->sendTo(MemoryNet::at(2), makePacket(PacketKind::Reliable, 0, {0x77, 1, 2}));
  link.ta->sendTo(MemoryNet::at(2), makePacket(PacketKind::Reliable, 1, encodeMessage(Chat{"after"})));
  link.run(0.1);
  CHECK(chats(link.gotB) == std::vector<std::string>{"after"});
}

TEST(connection_refuses_sequences_far_ahead) {
  Link link;
  link.ta->sendTo(MemoryNet::at(2),
                  makePacket(PacketKind::Reliable, Connection::kReceiveWindow + 5, encodeMessage(Chat{"x"})));
  link.run(0.1);
  CHECK(link.gotB.empty());
  CHECK(link.gotA.empty());  // no ack came back, so the sender will retry later
}

// ------------------------------------------------------------------ sessions

TEST(guest_joins_and_follows_host_smoothly) {
  Pair p;
  p.run(5);
  CHECK(p.host->connected() && p.guest->connected());

  // Each side sees the other about 0.1 s in the past (2 ticks at 20/s).
  auto seenByGuest = p.guestGame.remote();
  CHECK(seenByGuest && distance(seenByGuest->pos, p.hostGame.positionAt(p.t - 0.1)) < 0.5f);
  auto seenByHost = p.hostGame.remote();
  CHECK(seenByHost && distance(seenByHost->pos, p.guestGame.positionAt(p.t - 0.1)) < 0.5f);
}

TEST(sessions_work_over_real_udp) {
  SimGame hostGame(1, 10.f, 1.f), guestGame(1, 3.f, -1.f);
  SessionConfig hc;
  hc.port = 47711;
  hc.gameBuild = 7;
  auto host = createHost(hostGame, hc);
  SessionConfig gc = hc;
  gc.hostAddress = "127.0.0.1";
  auto guest = createGuest(guestGame, gc);
  CHECK(host && guest);
  if (!host || !guest) return;
  for (double t = 0; t < 2; t += 1.0 / 60) {
    hostGame.advance(1.0 / 60);
    host->update(t);
    guest->update(t);
  }
  CHECK(host->connected() && guest->connected() && guestGame.remote());
}

TEST(sessions_hold_up_on_a_bad_network) {
  // 60-100 ms each way, 10% loss, 5% duplicates.
  Pair p({0.06, 0.04, 0.1, 0.05, 7});
  p.run(4);
  CHECK(p.host->connected() && p.guest->connected());

  for (int i = 0; i < 20; ++i) p.guest->sendChat("line " + std::to_string(i));
  p.run(3);
  int found = 0;
  for (auto& line : p.hostLog) {
    if (line == "Guest: line " + std::to_string(found)) ++found;  // in order
  }
  CHECK(found == 20);

  // Position lags by the 0.1 s buffer plus the one-way latency.
  auto seen = p.guestGame.remote();
  CHECK(seen && distance(seen->pos, p.hostGame.positionAt(p.t - 0.18)) < 1.5f);
}

TEST(guest_clock_follows_host) {
  Pair p;
  p.hostGame.writeClock({1, 2, 15, 18, 30});
  p.run(3);
  auto host = p.hostGame.readClock();
  auto guest = p.guestGame.readClock();
  // Clock packets go out once a second; the guest may be up to a minute behind.
  CHECK(guest.day == 15 && guest.season == 2 && host.hour == guest.hour &&
        (host.minute - guest.minute == 0 || host.minute - guest.minute == 1));
}

TEST(host_rejects_a_different_game_build) {
  Pair p({}, 8);
  p.run(2);
  CHECK(p.guest->failed() && !p.guest->connected() && !p.host->connected());
  CHECK(contains(p.guestLog, "Host refused: Game version mismatch"));
}

TEST(session_holds_only_one_guest) {
  Pair p;
  SimGame thirdGame(1, 1.f, 1.f);
  SessionConfig c;
  c.gameBuild = 7;
  auto third = createGuest(thirdGame, c, p.net.open(6000), MemoryNet::at(Pair::kHostPort));
  const double dt = 1.0 / 60;
  for (int i = 0; i < 120; ++i, p.t += dt) {
    p.host->update(p.t);
    p.guest->update(p.t);
    third->update(p.t);
  }
  CHECK(p.guest->connected());
  CHECK(third->failed());
}

TEST(restarted_guest_on_the_same_address_gets_back_in) {
  Pair p;
  p.run(1);
  for (int i = 0; i < 3; ++i) p.guest->sendChat("before " + std::to_string(i));
  p.run(1);
  CHECK(p.host->connected() && contains(p.hostLog, "Guest: before 2"));
  // The guest's game crashes and restarts from the same address and port:
  // a new nonce tells the host it's a new connection, not a stranger.
  p.guestLog.clear();
  p.guest = createGuest(p.guestGame, p.config(7, p.guestLog, "Guest"), p.net.open(Pair::kGuestPort),
                        MemoryNet::at(Pair::kHostPort));
  p.run(1);
  CHECK(p.guest->connected() && contains(p.guestLog, "Joined the host"));
  for (int i = 0; i < 3; ++i) p.guest->sendChat("back " + std::to_string(i));
  p.run(1);
  CHECK(contains(p.hostLog, "Guest: back 2"));
}

TEST(guest_is_leashed_to_the_host_map) {
  Pair p;
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
  Pair p;
  p.run(2);
  p.guest->leave();
  p.run(0.5);
  CHECK(!p.host->connected() && !p.hostGame.remote());

  Pair q;
  q.run(2);
  q.run(6, /*hostRuns=*/false);  // host freezes
  CHECK(!q.guest->connected() && !q.guestGame.remote() && !q.guest->failed());
  q.run(2);  // host comes back; guest rejoins
  CHECK(q.guest->connected() && q.host->connected());
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
