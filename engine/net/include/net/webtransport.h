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
// Dedupe by stream offset: every sendDatagram carries one stream-0 datagram
// and is assigned the next byte offset in send order; a payload already
// delivered at an earlier offset is a flap retransmit and is delivered to
// Listener::onDatagram exactly once.
//
// Error shape: every failure surfaces as onState(WtState::kClosed, reason)
// with a non-empty reason and never escapes as an exception. Migration
// emits kMigrating then kConnected, in order.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

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
  // Stream-0 offset ledger: payload -> byte offset of first delivery.
  std::unordered_map<std::string, std::uint64_t> seen_offset_;
  std::uint64_t next_offset_ = 0;
};

}  // namespace net
}  // namespace muse
