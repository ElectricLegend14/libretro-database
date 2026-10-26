#include "coop/session.hpp"

#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "coop/connection.hpp"
#include "coop/udp.hpp"

namespace coop {

// ------------------------------------------------------------------- sessions

namespace {

// Host-side limits on what the guest may ask for.
constexpr float kMaxAttackDrift = 4;  // attack origin vs. last reported position
constexpr float kPickupRange = 3;
constexpr double kAttacksPerSec = 8;
constexpr double kAttackBurst = 4;
// Guest: how long fainted before waking up beside the host.
constexpr double kFaintSec = 3;
// After waking, bites the host settled against the old position are ignored.
constexpr double kWakeGraceSec = 2;

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
    bool wasConnected = conn_.has_value();
    conn_.reset();
    remote_.reset();
    hidePartner();
    if (wasConnected) onDisconnected();
  }

  virtual void onDisconnected() {}

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
    if (!conn_) return;
    if (tickDue(now)) {
      conn_->sendUnreliable(localState());
      if (tick_ % clockEveryTicks() == 0) conn_->sendUnreliable(ClockSync{game_.readClock()});
      syncEntities();
    }
    for (auto& hurt : game_.drainHitsOnRemotePlayer()) conn_->sendReliable(PlayerHurt{hurt});
    drawPartner(now);
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
    } else if (auto* attack = std::get_if<AttackRequest>(&msg)) {
      onAttack(*attack, now);
    } else if (auto* pickup = std::get_if<PickupRequest>(&msg)) {
      onPickup(*pickup);
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

  // Tells the guest which entities appeared, moved and went away since the
  // last tick. Spawns and despawns are reliable; movement is not.
  void syncEntities() {
    std::map<uint32_t, EntityState> current;
    for (auto& e : game_.readEntities()) current.emplace(e.id, e);

    for (auto it = known_.begin(); it != known_.end();) {
      auto now = current.find(it->first);
      bool same = now != current.end() && now->second.category == it->second.category &&
                  now->second.kind == it->second.kind;  // an ID reused for something else is a new entity
      if (same) {
        ++it;
        continue;
      }
      conn_->sendReliable(EntityDespawn{it->first, despawnReason(it->first, it->second)});
      it = known_.erase(it);
    }

    EntityUpdates batch{tick_, {}};
    for (auto& [id, e] : current) {
      auto [it, isNew] = known_.insert_or_assign(id, e);
      if (isNew) {
        conn_->sendReliable(EntitySpawn{tick_, e});
        continue;
      }
      batch.entities.push_back({id, e.pos, e.facing, e.anim, e.hp});
      if (batch.entities.size() == EntityUpdates::kMaxPerPacket) {
        conn_->sendUnreliable(batch);
        batch.entities.clear();
      }
    }
    if (!batch.entities.empty()) conn_->sendUnreliable(batch);
  }

  DespawnReason despawnReason(uint32_t id, const EntityState& last) {
    if (pickedUp_.erase(id)) return DespawnReason::PickedUp;
    if (last.category == EntityCategory::Monster && last.hp == 0) return DespawnReason::Killed;
    return DespawnReason::Gone;
  }

  // The guest's attack runs in the host's world. The host checks it came from
  // where the guest actually is and isn't arriving faster than anyone can swing.
  void onAttack(const AttackRequest& request, double now) {
    auto guest = remote_.latest();
    attackBudget_ = std::min(kAttackBurst, attackBudget_ + (now - budgetAt_) * kAttacksPerSec);
    budgetAt_ = now;
    if (!guest || attackBudget_ < 1) {
      conn_->sendReliable(HitResult{request.requestId, {}});
      return;
    }
    attackBudget_ -= 1;

    AttackIntent intent = request.intent;
    if (distance(intent.origin, guest->pos) > kMaxAttackDrift) intent.origin = guest->pos;
    auto hits = game_.resolveGuestAttack(intent, *guest);
    if (hits.size() > HitResult::kMaxHits) hits.resize(HitResult::kMaxHits);
    for (auto& hit : hits) {
      if (auto it = known_.find(hit.targetId); it != known_.end()) it->second.hp = hit.hpAfter;
    }
    conn_->sendReliable(HitResult{request.requestId, std::move(hits)});
  }

  void onPickup(const PickupRequest& request) {
    auto guest = remote_.latest();
    auto it = known_.find(request.entityId);
    bool inReach = guest && it != known_.end() && it->second.category == EntityCategory::Drop &&
                   it->second.mapId == guest->mapId && distance(it->second.pos, guest->pos) <= kPickupRange;
    auto item = inReach ? game_.takeDropForGuest(request.entityId) : std::nullopt;
    if (item) pickedUp_.insert(request.entityId);
    conn_->sendReliable(PickupResult{request.entityId, item.has_value(), item.value_or(0)});
  }

  void onDisconnected() override {
    known_.clear();
    pickedUp_.clear();
  }

  uint32_t guestNonce_ = 0;
  std::string guestName_;
  std::map<uint32_t, EntityState> known_;  // what the guest has been told exists
  std::set<uint32_t> pickedUp_;
  double attackBudget_ = kAttackBurst;
  double budgetAt_ = 0;
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
    drawEntities(now);
    sendActions();
    if (faintedAt_ && now - *faintedAt_ >= kFaintSec) wakeUp(now);
    if (!faintedAt_) leash(now);
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
    game_.setLocalEntitiesSuppressed(true);
  }

  void onMessage(const Message& msg, double now) override {
    if (auto* state = std::get_if<PlayerState>(&msg)) {
      remote_.onState(*state, now);
    } else if (auto* clock = std::get_if<ClockSync>(&msg)) {
      game_.writeClock(clock->clock);
    } else if (auto* spawn = std::get_if<EntitySpawn>(&msg)) {
      auto& mirror = entities_.insert_or_assign(spawn->entity.id, Mirror{spawn->entity, track()}).first->second;
      mirror.track.push(spawn->tick, spawn->entity, now);
    } else if (auto* updates = std::get_if<EntityUpdates>(&msg)) {
      for (auto& delta : updates->entities) {
        auto it = entities_.find(delta.id);
        if (it == entities_.end()) continue;  // its spawn hasn't arrived yet
        EntityState e = it->second.base;
        e.pos = delta.pos;
        e.facing = delta.facing;
        e.anim = delta.anim;
        e.hp = delta.hp;
        it->second.track.push(updates->tick, e, now);
      }
    } else if (auto* despawn = std::get_if<EntityDespawn>(&msg)) {
      if (auto it = entities_.find(despawn->id); it != entities_.end()) {
        if (it->second.shown) game_.removeRemoteEntity(despawn->id, despawn->reason == DespawnReason::Killed);
        entities_.erase(it);
      }
    } else if (auto* result = std::get_if<HitResult>(&msg)) {
      game_.showHits(result->hits);
    } else if (auto* hurt = std::get_if<PlayerHurt>(&msg)) {
      if (faintedAt_ || now < graceUntil_) return;
      game_.applyHitToLocalPlayer(hurt->hurt);
      if (game_.readLocalPlayer().hp == 0) {
        log("Fainted! Waking up beside the host soon");
        faintedAt_ = now;
      }
    } else if (auto* pickup = std::get_if<PickupResult>(&msg)) {
      if (pickup->granted) game_.giveItem(pickup->itemId);
    } else if (auto* chat = std::get_if<Chat>(&msg)) {
      log("Host: " + chat->text);
    } else if (std::holds_alternative<Goodbye>(msg)) {
      log("The host ended the session");
      disconnect(std::nullopt);
      failed_ = true;
    }
    // Repeated Welcomes once connected need nothing.
  }

  Interpolated<EntityState> track() const { return {cfg_.tickRate, cfg_.interpDelayTicks}; }

  // Shows the host's entities that are on the guest's map, smoothed.
  void drawEntities(double now) {
    uint16_t map = game_.readLocalPlayer().mapId;
    for (auto& [id, mirror] : entities_) {
      auto view = mirror.track.view(now);
      if (view && view->mapId == map) {
        game_.upsertRemoteEntity(*view);
        mirror.shown = true;
      } else if (mirror.shown) {
        game_.removeRemoteEntity(id, false);
        mirror.shown = false;
      }
    }
  }

  // Forwards the local player's attacks and pick-ups for the host to settle.
  void sendActions() {
    auto attacks = game_.drainLocalAttacks();
    auto pickups = game_.drainLocalPickups();
    if (faintedAt_) return;  // can't act while fainted
    for (auto& intent : attacks) conn_->sendReliable(AttackRequest{nextRequestId_++, intent});
    for (auto id : pickups) conn_->sendReliable(PickupRequest{id});
  }

  void wakeUp(double now) {
    faintedAt_.reset();
    graceUntil_ = now + kWakeGraceSec;
    auto host = remote_.latest();
    if (!host) return;
    auto self = game_.readLocalPlayer();
    game_.reviveLocalPlayer(host->mapId, host->pos, std::max<uint32_t>(1, self.maxHp / 4));
    log("Woke up beside the host");
  }

  void onDisconnected() override {
    for (auto& [id, mirror] : entities_) {
      if (mirror.shown) game_.removeRemoteEntity(id, false);
    }
    entities_.clear();
    faintedAt_.reset();
    game_.setLocalEntitiesSuppressed(false);
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

  struct Mirror {
    EntityState base;  // fields that don't change after spawn
    Interpolated<EntityState> track;
    bool shown = false;
  };

  Endpoint host_;
  uint32_t nonce_ = randomNonce();
  bool failed_ = false;
  double nextHello_ = 0;
  std::optional<double> apartSince_;
  std::map<uint32_t, Mirror> entities_;
  uint32_t nextRequestId_ = 1;
  std::optional<double> faintedAt_;
  double graceUntil_ = 0;
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
