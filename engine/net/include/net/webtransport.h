#pragma once

// WebTransport session over HTTP/3 with connection-migration survival
// (FR-NET-005 / AC-NET-008 WT half; RULE-NET-002).
//
// The session state machine runs over the injected HTTP/3 handle
// (WebTransportConfig::transport): the handshake request is sent through
// ITransport::execute, and migration re-establishes the path without
// losing session state. No sockets, no QUIC, no TLS, no clock reads: all
// listener callbacks fire synchronously before connect()/sendDatagram()/
// migrate() return.
//
// Dedupe by per-send sequence: every sendDatagram call is assigned the
// next monotonic sequence number in send order, independent of payload
// length. Delivery is keyed by SEQUENCE (delivered-sequence set), never
// by payload content nor by byte offset: identical payloads — including
// consecutive empty datagrams — each deliver; the same sequence number
// re-presented (flap retransmit) is absorbed and Listener::onDatagram
// fires once per sequence number.
//
// Error shape: every failure surfaces as onState(WtState::kClosed, reason)
// with a non-empty reason and never escapes as an exception. Migration
// emits kMigrating then kConnected, in order.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "net/transport.h"

namespace muse {
namespace net {

enum class WtState { kConnecting, kConnected, kMigrating, kClosed };

struct WebTransportConfig {
  std::shared_ptr<ITransport> transport;
};

class WebTransport {
 public:
  struct Listener {
    std::function<void(WtState, std::string)> onState;
    std::function<void(std::string)> onDatagram;
  };

  explicit WebTransport(WebTransportConfig cfg);
  void connect(std::string url);
  void sendDatagram(std::string d);
  void migrate(std::string newNetwork);
  void setListener(Listener l);

 private:
  void emitState(WtState s, const std::string& reason);
  void reportClosed(const std::string& reason) noexcept;

  WebTransportConfig cfg_;
  Listener listener_;
  WtState state_ = WtState::kClosed;
  bool open_ = false;
  std::string url_;
  std::string network_;
  // Per-send sequence ledger: set of delivered sequence numbers. Keyed
  // by SEND ORDER, never by payload content nor byte offset: identical
  // payloads (including consecutive empty datagrams) each own a distinct
  // sequence number and each deliver; the same sequence number twice
  // delivers once. Bounded retention window: only the most recent
  // kMaxRetainedSeqs sequences are kept; older entries are evicted as new
  // sends arrive (duplicates only arrive via flap retransmit of recent
  // sends, so evicting the distant past cannot re-deliver).
  static constexpr std::size_t kMaxRetainedSeqs = 4096;
  std::unordered_set<std::uint64_t> delivered_seqs_;
  std::uint64_t next_seq_ = 0;
};

}  // namespace net
}  // namespace muse
