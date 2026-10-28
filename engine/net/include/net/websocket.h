#pragma once

// WebSocket client with permessage-deflate + subprotocol negotiation
// (FR-NET-005 / AC-NET-008 WS half; FR-NET-009 / AC-NET-017 WS deflate half).
//
// Frame/deflate logic runs over the injected handshake transport
// (WebSocketConfig::httpHandshake): the handshake request is sent through
// ITransport::execute, and each subsequent execute replays the next scripted
// peer response, which is delivered to Listener::onMessage as one inbound
// message. No sockets, no threads, no clock reads: all listener callbacks
// fire synchronously before connect()/send()/close() return.
//
// Error shape: every failure surfaces as onState(WsState::kClosed, reason)
// with a non-empty reason and never escapes as an exception. The compressed
// and uncompressed paths deliver identical bytes.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"

namespace muse {
namespace net {

enum class WsState { kConnecting, kOpen, kClosing, kClosed };

struct WebSocketConfig {
  bool permessageDeflate = true;
  std::vector<std::string> subprotocols;
  std::shared_ptr<ITransport> httpHandshake;
};

class WebSocket {
 public:
  struct Listener {
    std::function<void()> onOpen;
    std::function<void(std::string, bool)> onMessage;
    std::function<void(WsState, std::string)> onState;
  };

  explicit WebSocket(WebSocketConfig cfg);
  void connect(std::string url);
  void send(std::string msg, bool binary);
  void close();
  void setListener(Listener l);

  // Negotiated subprotocol value (empty when none was negotiated).
  std::string selectedSubprotocol() const;

 private:
  void reportClosed(const std::string& reason) noexcept;

  WebSocketConfig cfg_;
  Listener listener_;
  WsState state_ = WsState::kClosed;
  bool open_ = false;
  std::string url_;
  std::string selected_;
};

}  // namespace net
}  // namespace muse
