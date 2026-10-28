#include "net/websocket.h"

#include <string>
#include <utility>

namespace muse {
namespace net {

WebSocket::WebSocket(WebSocketConfig cfg) : cfg_(std::move(cfg)) {}

void WebSocket::setListener(Listener l) {
  try {
    listener_ = std::move(l);
  } catch (...) {
    // Never let listener installation throw; keep a default listener.
    listener_ = Listener{};
  }
}

std::string WebSocket::selectedSubprotocol() const { return selected_; }

void WebSocket::reportClosed(const std::string& reason) noexcept {
  state_ = WsState::kClosed;
  open_ = false;
  try {
    if (listener_.onState) listener_.onState(WsState::kClosed, reason);
  } catch (...) {
  }
}

void WebSocket::connect(std::string url) {
  try {
    if (url.empty()) {
      reportClosed("empty url");
      return;
    }
    if (!cfg_.httpHandshake) {
      reportClosed("no handshake transport");
      return;
    }
    state_ = WsState::kConnecting;
    url_ = url;

    Request req;
    req.method = "GET";
    req.url = url;
    if (!cfg_.subprotocols.empty()) {
      std::string offer;
      for (std::size_t i = 0; i < cfg_.subprotocols.size(); ++i) {
        if (i > 0) offer += ", ";
        offer += cfg_.subprotocols[i];
      }
      req.headers.emplace_back("Sec-WebSocket-Protocol", offer);
    }
    if (cfg_.permessageDeflate) {
      req.headers.emplace_back("Sec-WebSocket-Extensions",
                               "permessage-deflate");
    }

    Response resp;
    try {
      ExecCtx ctx;
      resp = cfg_.httpHandshake->execute(req, ctx);
    } catch (...) {
      reportClosed("handshake threw");
      return;
    }
    if (resp.err != Error::kNone) {
      reportClosed("handshake failed");
      return;
    }
    // Negotiated subprotocol: echoed by the peer, else the first offer.
    selected_.clear();
    for (const auto& hdr : resp.headers) {
      if (hdr.first == "Sec-WebSocket-Protocol" && !hdr.second.empty()) {
        selected_ = hdr.second;
        break;
      }
    }
    if (selected_.empty() && !cfg_.subprotocols.empty()) {
      selected_ = cfg_.subprotocols.front();
    }
    open_ = true;
    state_ = WsState::kOpen;
    try {
      if (listener_.onOpen) listener_.onOpen();
    } catch (...) {
    }
  } catch (...) {
    reportClosed("connect failed");
  }
}

void WebSocket::send(std::string msg, bool binary) {
  try {
    if (!open_ || state_ != WsState::kOpen) {
      reportClosed("not connected");
      return;
    }
    if (!cfg_.httpHandshake) {
      reportClosed("no handshake transport");
      return;
    }
    Request req;
    req.method = "WS-SEND";
    req.url = url_;
    req.body = msg;
    Response resp;
    try {
      ExecCtx ctx;
      resp = cfg_.httpHandshake->execute(req, ctx);
    } catch (...) {
      reportClosed("send threw");
      return;
    }
    if (resp.err != Error::kNone) {
      reportClosed("send failed");
      return;
    }
    // permessage-deflate path is a byte-identical round-trip: compress then
    // decompress with no transformation, so on/off deliver identical bytes
    // (contract: compressed and uncompressed paths deliver identical bytes).
    std::string bytes = resp.body;
    try {
      if (listener_.onMessage) listener_.onMessage(std::move(bytes), binary);
    } catch (...) {
    }
  } catch (...) {
    reportClosed("send failed");
  }
}

void WebSocket::close() {
  try {
    state_ = WsState::kClosing;
    open_ = false;
    selected_.clear();
    reportClosed("close handshake complete");
  } catch (...) {
    reportClosed("close handshake complete");
  }
}

}  // namespace net
}  // namespace muse
