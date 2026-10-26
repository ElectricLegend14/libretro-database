#include "coop/connection.hpp"

#include <algorithm>

namespace coop {
namespace {

// Distance from `from` to `seq`, allowing for 16-bit wraparound.
int seqDelta(uint16_t seq, uint16_t from) { return static_cast<int16_t>(static_cast<uint16_t>(seq - from)); }

}  // namespace

void Connection::sendUnreliable(const Message& msg) { send(makePacket(PacketKind::Unreliable, 0, encodeMessage(msg))); }

uint16_t Connection::sendReliable(const Message& msg) {
  uint16_t seq = nextSendSeq_++;
  Pending pending{seq, makePacket(PacketKind::Reliable, seq, encodeMessage(msg)), now_, now_};
  send(pending.packet);
  unacked_.push_back(std::move(pending));
  return seq;
}

bool Connection::acked(uint16_t seq) const {
  return std::none_of(unacked_.begin(), unacked_.end(), [seq](const Pending& p) { return p.seq == seq; });
}

std::vector<Message> Connection::onPacket(const PacketView& packet) {
  std::vector<Message> released;
  switch (packet.kind) {
    case PacketKind::Unreliable:
      if (auto msg = decodeMessage(packet.body, packet.bodyLen)) released.push_back(std::move(*msg));
      break;

    case PacketKind::Ack: {
      auto acked = std::find_if(unacked_.begin(), unacked_.end(),
                                [&](const Pending& p) { return p.seq == packet.seq; });
      if (acked != unacked_.end()) unacked_.erase(acked);
      break;
    }

    case PacketKind::Reliable: {
      int delta = seqDelta(packet.seq, nextDeliverSeq_);
      if (delta >= kReceiveWindow) break;  // too far ahead: no ack, the sender will retry
      send(makePacket(PacketKind::Ack, packet.seq, {}));
      if (delta < 0) break;  // a resend of something already delivered; the ack above was lost

      early_.try_emplace(packet.seq, decodeMessage(packet.body, packet.bodyLen));
      for (auto next = early_.find(nextDeliverSeq_); next != early_.end(); next = early_.find(nextDeliverSeq_)) {
        if (next->second) released.push_back(std::move(*next->second));
        early_.erase(next);
        ++nextDeliverSeq_;
      }
      break;
    }
  }
  return released;
}

void Connection::update(double now) {
  now_ = now;
  for (auto& pending : unacked_) {
    if (now - pending.lastSent < kResendSec) continue;
    send(pending.packet);
    pending.lastSent = now;
  }
}

}  // namespace coop
