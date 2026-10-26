#pragma once

#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "coop/protocol.hpp"
#include "coop/transport.hpp"

namespace coop {

// One link to one peer, with two channels on top of plain datagrams:
//
// - unreliable: sent once, may be lost or arrive out of order. For state
//   that's resent every tick anyway (positions, the clock).
// - reliable: resent until acknowledged and delivered exactly once, in the
//   order sent. For events (hits, pickups, chat, map changes).
class Connection {
 public:
  // How long to wait for an ack before sending a reliable message again.
  static constexpr double kResendSec = 0.1;
  // Sequence numbers further ahead than this are refused (not acked), so the
  // sender retries once the gap before them has been filled.
  static constexpr int kReceiveWindow = 1024;

  Connection(ITransport& transport, Endpoint peer) : transport_(transport), peer_(peer) {}

  const Endpoint& peer() const { return peer_; }

  void sendUnreliable(const Message& msg);
  // Returns the message's sequence number, for acked().
  uint16_t sendReliable(const Message& msg);
  // True once the peer has confirmed it received that message.
  bool acked(uint16_t seq) const;

  // Feeds one packet from the peer. Returns the messages it releases, in
  // order: an unreliable message, or any reliable ones now in sequence.
  std::vector<Message> onPacket(const PacketView& packet);

  // Resends what's overdue. Call once per frame.
  void update(double now);

  size_t unackedCount() const { return unacked_.size(); }
  // How long the oldest unacknowledged message has been waiting.
  double oldestUnackedAge() const { return unacked_.empty() ? 0 : now_ - unacked_.front().firstSent; }

 private:
  struct Pending {
    uint16_t seq;
    std::vector<uint8_t> packet;
    double firstSent;
    double lastSent;
  };

  void send(const std::vector<uint8_t>& packet) { transport_.sendTo(peer_, packet); }

  ITransport& transport_;
  Endpoint peer_;
  double now_ = 0;

  uint16_t nextSendSeq_ = 0;
  std::deque<Pending> unacked_;

  uint16_t nextDeliverSeq_ = 0;
  // Arrived ahead of a gap. nullopt marks one that didn't decode, which is
  // skipped rather than blocking everything after it.
  std::map<uint16_t, std::optional<Message>> early_;
};

}  // namespace coop
