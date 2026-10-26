#include "coop/session.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "coop/connection.hpp"
#include "coop/udp.hpp"

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

uint32_t randomNonce() {
  static std::mt19937 rng(std::random_device{}());
  uint32_t n;
  do n = static_cast<uint32_t>(rng()); while (n == 0);
  return n;
}

// What host and guest share: the transport, the link to the partner, the
// tick clock and the partner's body.
class Peer {
 protected:
  Peer(IGameAdapter& game, SessionConfig config, std::unique_ptr<ITransport> transport)
      : game_(game),
        cfg_(std::move(config)),
        transport_(std::move(transport)),
        remote_(cfg_.tickRate, cfg_.interpDelayTicks) {}

  void log(const std::string& line) {
    if (cfg_.log) cfg_.log(line);
  }

  // Sends one message outside any connection, e.g. a handshake reply.
  void sendLoose(const Endpoint& to, const Message& msg) {
    transport_->sendTo(to, makePacket(PacketKind::Unreliable, 0, encodeMessage(msg)));
  }

  // Reads every waiting datagram. Packets from the partner go through the
  // connection; anything else is offered to onStranger as a loose message.
  void pump(double now) {
    transport_->advance(now);
    while (auto d = transport_->receive()) {
      auto packet = parsePacket(d->data.data(), d->data.size());
      if (!packet) continue;
      if (conn_ && d->from == conn_->peer()) {
        lastHeard_ = now;
        for (auto& msg : conn_->onPacket(*packet)) {
          onMessage(msg, now);
          if (!conn_) break;  // the message ended the connection
        }
      } else if (packet->kind == PacketKind::Unreliable) {
        if (auto msg = decodeMessage(packet->body, packet->bodyLen)) onStranger(d->from, *msg, now);
      }
    }
    if (conn_) conn_->update(now);
  }

  virtual void onMessage(const Message& msg, double now) = 0;
  virtual void onStranger(const Endpoint& from, const Message& msg, double now) = 0;

  // Silent for too long, or a reliable message stuck for too long.
  bool linkLost(double now) const {
    return conn_ && (now - lastHeard_ > cfg_.timeoutSec || conn_->oldestUnackedAge() > cfg_.timeoutSec);
  }

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

  // Ends the link: optionally says goodbye, then forgets the partner.
  void disconnect(std::optional<GoodbyeReason> goodbye) {
    if (conn_ && goodbye) conn_->sendUnreliable(Goodbye{*goodbye});
    conn_.reset();
    remote_.reset();
    hidePartner();
  }

  IGameAdapter& game_;
  SessionConfig cfg_;
  std::unique_ptr<ITransport> transport_;
  std::optional<Connection> conn_;
  RemotePlayer remote_;
  uint32_t tick_ = 0;
  double lastHeard_ = 0;

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
  HostSession(IGameAdapter& game, SessionConfig config, std::unique_ptr<ITransport> transport)
      : Peer(game, std::move(config), std::move(transport)) {
    log("Hosting on port " + std::to_string(transport_->localPort()));
  }

  void update(double now) override {
    pump(now);
    if (linkLost(now)) {
      log(guestName_ + " timed out");
      disconnect(GoodbyeReason::Timeout);
    }
    if (tickDue(now) && conn_) {
      conn_->sendUnreliable(localState());
      if (tick_ % clockEveryTicks() == 0) conn_->sendUnreliable(ClockSync{game_.readClock()});
    }
    if (conn_) drawPartner(now);
  }

  bool connected() const override { return conn_.has_value(); }

  void sendChat(const std::string& text) override {
    if (conn_) conn_->sendReliable(Chat{text});
  }

  void leave() override { disconnect(GoodbyeReason::Quit); }

 private:
  void onMessage(const Message& msg, double now) override {
    if (auto* hello = std::get_if<Hello>(&msg)) {
      onHello(conn_->peer(), *hello, now);
    } else if (auto* state = std::get_if<PlayerState>(&msg)) {
      remote_.onState(*state, now);
    } else if (auto* chat = std::get_if<Chat>(&msg)) {
      log(guestName_ + ": " + chat->text);
    } else if (std::holds_alternative<Goodbye>(msg)) {
      log(guestName_ + " left");
      disconnect(std::nullopt);
    }
  }

  void onStranger(const Endpoint& from, const Message& msg, double now) override {
    if (auto* hello = std::get_if<Hello>(&msg)) onHello(from, *hello, now);
  }

  void onHello(const Endpoint& from, const Hello& hello, double now) {
    auto reject = [&](const std::string& reason) {
      sendLoose(from, Welcome{false, 0, hello.nonce, reason});
      log("Refused " + from.toString() + ": " + reason);
    };
    if (conn_ && conn_->peer() != from) return reject("Session is full");
    if (hello.modVersion != kModVersion) return reject("Mod version mismatch");
    if (hello.gameBuild != cfg_.gameBuild) return reject("Game version mismatch");

    if (!conn_ || hello.nonce != guestNonce_) {
      // A new guest, or the same one starting over: begin a fresh connection.
      if (conn_) disconnect(std::nullopt);
      conn_.emplace(*transport_, from);
      guestNonce_ = hello.nonce;
      guestName_ = hello.name;
      lastHeard_ = now;
      log(guestName_ + " joined from " + from.toString());
      conn_->sendUnreliable(ClockSync{game_.readClock()});
    }
    // Answer every Hello: an earlier Welcome may have been lost.
    conn_->sendUnreliable(Welcome{true, 1, hello.nonce, ""});
  }

  uint32_t clockEveryTicks() const { return std::max<uint32_t>(1, static_cast<uint32_t>(cfg_.tickRate)); }

  uint32_t guestNonce_ = 0;
  std::string guestName_;
};

class GuestSessionImpl final : public GuestSession, Peer {
 public:
  GuestSessionImpl(IGameAdapter& game, SessionConfig config, std::unique_ptr<ITransport> transport, Endpoint host)
      : Peer(game, std::move(config), std::move(transport)), host_(host) {
    log("Joining " + host_.toString());
  }

  void update(double now) override {
    pump(now);
    if (failed_) return;

    if (!conn_) {
      if (now >= nextHello_) {
        sendLoose(host_, Hello{kModVersion, cfg_.gameBuild, nonce_, cfg_.playerName});
        nextHello_ = now + 1;
      }
      return;
    }
    if (linkLost(now)) {
      log("Lost the host; retrying");
      disconnect(std::nullopt);
      nonce_ = randomNonce();  // the host may have dropped us: start a new connection
      return;
    }
    if (tickDue(now)) conn_->sendUnreliable(localState());
    drawPartner(now);
    leash(now);
  }

  bool connected() const override { return conn_.has_value(); }
  bool failed() const override { return failed_; }

  void sendChat(const std::string& text) override {
    if (conn_) conn_->sendReliable(Chat{text});
  }

  void leave() override {
    disconnect(GoodbyeReason::Quit);
    failed_ = true;
  }

 private:
  // Before the Welcome arrives, the host's packets come in loose.
  void onStranger(const Endpoint& from, const Message& msg, double now) override {
    if (from != host_) return;
    auto* welcome = std::get_if<Welcome>(&msg);
    if (!welcome || welcome->nonce != nonce_ || failed_) return;
    if (!welcome->accepted) {
      log("Host refused: " + welcome->reason);
      failed_ = true;
      return;
    }
    log("Joined the host");
    conn_.emplace(*transport_, host_);
    lastHeard_ = now;
  }

  void onMessage(const Message& msg, double now) override {
    if (auto* state = std::get_if<PlayerState>(&msg)) {
      remote_.onState(*state, now);
    } else if (auto* clock = std::get_if<ClockSync>(&msg)) {
      game_.writeClock(clock->clock);
    } else if (auto* chat = std::get_if<Chat>(&msg)) {
      log("Host: " + chat->text);
    } else if (std::holds_alternative<Goodbye>(msg)) {
      log("The host ended the session");
      disconnect(std::nullopt);
      failed_ = true;
    }
    // Repeated Welcomes once connected need nothing.
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
  uint32_t nonce_ = randomNonce();
  bool failed_ = false;
  double nextHello_ = 0;
  std::optional<double> apartSince_;
};

}  // namespace

std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config, std::unique_ptr<ITransport> transport) {
  return std::make_unique<HostSession>(game, std::move(config), std::move(transport));
}

std::unique_ptr<Session> createHost(IGameAdapter& game, SessionConfig config) {
  auto socket = UdpSocket::open(config.port);
  if (!socket) {
    if (config.log) config.log("Couldn't open UDP port " + std::to_string(config.port));
    return nullptr;
  }
  return createHost(game, std::move(config), std::make_unique<UdpSocket>(std::move(*socket)));
}

std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config,
                                          std::unique_ptr<ITransport> transport, Endpoint host) {
  return std::make_unique<GuestSessionImpl>(game, std::move(config), std::move(transport), host);
}

std::unique_ptr<GuestSession> createGuest(IGameAdapter& game, SessionConfig config) {
  auto host = resolveIPv4(config.hostAddress, config.port);
  auto socket = UdpSocket::open(0);
  if (!host || !socket) {
    if (config.log) config.log("Couldn't reach host address '" + config.hostAddress + "'");
    return nullptr;
  }
  return createGuest(game, std::move(config), std::make_unique<UdpSocket>(std::move(*socket)), *host);
}

}  // namespace coop
