#include "coop/session.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coop {

// ---------------------------------------------------------------- RemotePlayer

namespace {

constexpr size_t kMaxBufferedStates = 32;
// A tick this far behind the newest one means the sender restarted.
constexpr uint32_t kRestartGap = 100;

float lerp(float a, float b, double f) { return static_cast<float>(a + (b - a) * f); }

float lerpAngle(float a, float b, double f) {
  double delta = std::remainder(static_cast<double>(b) - a, 2 * std::numbers::pi);
  return static_cast<float>(a + delta * f);
}

}  // namespace

void RemotePlayer::onState(const PlayerState& state, double now) {
  if (!states_.empty() && state.tick + kRestartGap < states_.back().tick) states_.clear();

  auto at = std::lower_bound(states_.begin(), states_.end(), state.tick,
                             [](const PlayerState& s, uint32_t tick) { return s.tick < tick; });
  if (at != states_.end() && at->tick == state.tick) return;  // duplicate
  if (at == states_.end()) latestAt_ = now;
  states_.insert(at, state);
  while (states_.size() > kMaxBufferedStates) states_.pop_front();
}

std::optional<PlayerSnapshot> RemotePlayer::view(double now) const {
  if (states_.empty()) return std::nullopt;
  double estimatedTick = states_.back().tick + (now - latestAt_) * tickRate_;
  return sample(estimatedTick - delayTicks_);
}

std::optional<PlayerSnapshot> RemotePlayer::sample(double tick) const {
  if (states_.empty()) return std::nullopt;
  if (tick <= states_.front().tick) return states_.front().body;
  if (tick >= states_.back().tick) return states_.back().body;  // never extrapolate

  auto after = std::upper_bound(states_.begin(), states_.end(), tick,
                                [](double t, const PlayerState& s) { return t < s.tick; });
  auto before = std::prev(after);
  const PlayerSnapshot& a = before->body;
  const PlayerSnapshot& b = after->body;
  if (a.mapId != b.mapId) return b;  // don't slide across a map change

  double f = (tick - before->tick) / static_cast<double>(after->tick - before->tick);
  PlayerSnapshot out = f < 0.5 ? a : b;
  out.pos = {lerp(a.pos.x, b.pos.x, f), lerp(a.pos.y, b.pos.y, f), lerp(a.pos.z, b.pos.z, f)};
  out.facing = lerpAngle(a.facing, b.facing, f);
  return out;
}

// ------------------------------------------------------------------- sessions

namespace {

// What host and guest share: the socket, the tick clock and the partner.
class Peer {
 protected:
  Peer(IGameAdapter& game, SessionConfig config, UdpSocket socket)
      : game_(game),
        cfg_(std::move(config)),
        sock_(std::move(socket)),
        remote_(cfg_.tickRate, cfg_.interpDelayTicks) {}

  void log(const std::string& line) {
    if (cfg_.log) cfg_.log(line);
  }

  void send(const Endpoint& to, const Message& msg) { sock_.sendTo(to, encode(msg)); }

  // True once per network tick; advances tick_.
  bool tickDue(double now) {
    double step = 1.0 / cfg_.tickRate;
    if (!nextTick_) nextTick_ = now;
    if (now < *nextTick_) return false;
    *nextTick_ += step;
    if (now - *nextTick_ > 0.25) nextTick_ = now + step;  // after a stall, don't send a burst
    ++tick_;
    return true;
  }

  PlayerState localState() { return PlayerState{tick_, game_.readLocalPlayer()}; }

  void drawPartner(double now) {
    auto view = remote_.view(now);
    if (view && view->mapId == game_.readLocalPlayer().mapId) {
      game_.showRemotePlayer(*view);
      shown_ = true;
    } else {
      hidePartner();
    }
  }

  void forgetPartner() {
    remote_.reset();
    hidePartner();
  }

  IGameAdapter& game_;
  SessionConfig cfg_;
  UdpSocket sock_;
  RemotePlayer remote_;
  uint32_t tick_ = 0;

 private:
  void hidePartner() {
    if (!shown_) return;
    game_.hideRemotePlayer();
    shown_ = false;
  }

  std::optional<double> nextTick_;
  bool shown_ = false;
};

class HostSession final : public Session, Peer {
 public:
  HostSession(IGameAdapter& game, SessionConfig config, UdpSocket socket)
      : Peer(game, std::move(config), std::move(socket)) {
    log("Hosting on port " + std::to_string(sock_.localPort()));
  }

  void update(double now) override {
    while (auto d = sock_.receive()) handle(*d, now);

    if (guest_ && now - lastHeard_ > cfg_.timeoutSec) {
      log(guestName_ + " timed out");
      dropGuest();
    }
    if (tickDue(now) && guest_) {
      send(*guest_, localState());
      if (tick_ % clockEveryTicks() == 0) send(*guest_, ClockSync{game_.readClock()});
    }
    if (guest_) drawPartner(now);
  }

  bool connected() const override { return guest_.has_value(); }

  void sendChat(const std::string& text) override {
    if (guest_) send(*guest_, Chat{text});
  }

  void leave() override {
    if (!guest_) return;
    send(*guest_, Goodbye{GoodbyeReason::Quit});
    dropGuest();
  }

 private:
  void handle(const Datagram& d, double now) {
    auto msg = decode(d.data.data(), d.data.size());
    if (!msg) return;
    if (auto* hello = std::get_if<Hello>(&*msg)) {
      onHello(d.from, *hello, now);
      return;
    }
    if (!guest_ || d.from != *guest_) return;

    lastHeard_ = now;
    if (auto* state = std::get_if<PlayerState>(&*msg)) {
      remote_.onState(*state, now);
    } else if (auto* chat = std::get_if<Chat>(&*msg)) {
      log(guestName_ + ": " + chat->text);
    } else if (std::holds_alternative<Goodbye>(*msg)) {
      log(guestName_ + " left");
      dropGuest();
    }
  }

  void onHello(const Endpoint& from, const Hello& hello, double now) {
    auto reject = [&](const std::string& reason) {
      send(from, Welcome{false, 0, reason});
      log("Refused " + from.toString() + ": " + reason);
    };
    if (guest_ && *guest_ != from) return reject("Session is full");
    if (hello.modVersion != kModVersion) return reject("Mod version mismatch");
    if (hello.gameBuild != cfg_.gameBuild) return reject("Game version mismatch");

    bool isNew = !guest_;
    guest_ = from;
    guestName_ = hello.name;
    lastHeard_ = now;
    send(from, Welcome{true, 1, ""});  // resent on a repeated Hello, in case it was lost
    if (isNew) {
      log(guestName_ + " joined from " + from.toString());
      send(from, ClockSync{game_.readClock()});
    }
  }

  void dropGuest() {
    guest_.reset();
    forgetPartner();
  }

  uint32_t clockEveryTicks() const { return std::max<uint32_t>(1, static_cast<uint32_t>(cfg_.tickRate)); }

  std::optional<Endpoint> guest_;
  std::string guestName_;
  double lastHeard_ = 0;
};

class GuestSessionImpl final : public GuestSession, Peer {
 public:
  GuestSessionImpl(IGameAdapter& game, SessionConfig config, UdpSocket socket, Endpoint host)
      : Peer(game, std::move(config), std::move(socket)), host_(host) {
    log("Joining " + host_.toString());
  }

  void update(double now) override {
    while (auto d = sock_.receive()) {
      if (d->from == host_) handle(*d, now);
    }
    if (stopped_) return;

    if (!welcomed_) {
      if (now >= nextHello_) {
        send(host_, Hello{kModVersion, cfg_.gameBuild, cfg_.playerName});
        nextHello_ = now + 1;
      }
      return;
    }
    if (now - lastHeard_ > cfg_.timeoutSec) {
      log("Lost the host; retrying");
      welcomed_ = false;
      forgetPartner();
      return;
    }
    if (tickDue(now)) send(host_, localState());
    drawPartner(now);
    leash(now);
  }

  bool connected() const override { return welcomed_; }
  bool failed() const override { return stopped_; }

  void sendChat(const std::string& text) override {
    if (welcomed_) send(host_, Chat{text});
  }

  void leave() override {
    if (welcomed_) send(host_, Goodbye{GoodbyeReason::Quit});
    welcomed_ = false;
    stopped_ = true;
    forgetPartner();
  }

 private:
  void handle(const Datagram& d, double now) {
    auto msg = decode(d.data.data(), d.data.size());
    if (!msg) return;

    if (auto* welcome = std::get_if<Welcome>(&*msg)) {
      if (!welcome->accepted) {
        log("Host refused: " + welcome->reason);
        stopped_ = true;
      } else if (!welcomed_ && !stopped_) {
        log("Joined the host");
        welcomed_ = true;
        lastHeard_ = now;
      }
      return;
    }
    if (!welcomed_) return;

    lastHeard_ = now;
    if (auto* state = std::get_if<PlayerState>(&*msg)) {
      remote_.onState(*state, now);
    } else if (auto* clock = std::get_if<ClockSync>(&*msg)) {
      game_.writeClock(clock->clock);
    } else if (auto* chat = std::get_if<Chat>(&*msg)) {
      log("Host: " + chat->text);
    } else if (std::holds_alternative<Goodbye>(*msg)) {
      log("The host ended the session");
      welcomed_ = false;
      stopped_ = true;
      forgetPartner();
    }
  }

  // Follow the host to their map if we've been apart for too long.
  void leash(double now) {
    auto host = remote_.view(now);
    if (!host || host->mapId == game_.readLocalPlayer().mapId) {
      apartSince_.reset();
      return;
    }
    if (!apartSince_) {
      apartSince_ = now;
    } else if (now - *apartSince_ >= cfg_.leashSec) {
      log("Following the host to map " + std::to_string(host->mapId));
      game_.warpLocalPlayer(host->mapId, host->pos);
      apartSince_.reset();
    }
  }

  Endpoint host_;
  bool welcomed_ = false;
  bool stopped_ = false;
  double nextHello_ = 0;
  double lastHeard_ = 0;
  std::optional<double> apartSince_;
};

}  // namespace

std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config) {
  auto socket = UdpSocket::open(config.port);
  if (!socket) {
    if (config.log) config.log("Couldn't open UDP port " + std::to_string(config.port));
    return nullptr;
  }
  return std::make_unique<HostSession>(game, std::move(config), std::move(*socket));
}

std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config) {
  auto host = resolveIPv4(config.hostAddress, config.port);
  auto socket = UdpSocket::open(0);
  if (!host || !socket) {
    if (config.log) config.log("Couldn't reach host address '" + config.hostAddress + "'");
    return nullptr;
  }
  return std::make_unique<GuestSessionImpl>(game, std::move(config), std::move(*socket), *host);
}

}  // namespace coop
