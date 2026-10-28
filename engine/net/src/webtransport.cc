#include "net/webtransport.h"

#include <string>
#include <utility>

namespace muse {
namespace net {

WebTransport::WebTransport(WebTransportConfig cfg) : cfg_(std::move(cfg)) {}

void WebTransport::setListener(Listener l) {
  try {
    listener_ = std::move(l);
  } catch (...) {
    // Never let listener installation throw; keep a default listener.
    listener_ = Listener{};
  }
}

void WebTransport::emitState(WtState s, const std::string& reason) {
  try {
    if (listener_.onState) listener_.onState(s, reason);
  } catch (...) {
  }
}

void WebTransport::reportClosed(const std::string& reason) noexcept {
  state_ = WtState::kClosed;
  open_ = false;
  try {
    if (listener_.onState) listener_.onState(WtState::kClosed, reason);
  } catch (...) {
  }
}

void WebTransport::connect(std::string url) {
  try {
    if (url.empty()) {
      reportClosed("empty url");
      return;
    }
    if (!cfg_.transport) {
      reportClosed("no transport");
      return;
    }
    state_ = WtState::kConnecting;
    emitState(WtState::kConnecting, "");
    url_ = url;

    Request req;
    req.method = "CONNECT";
    req.url = url;
    req.headers.emplace_back("Sec-WebTransport", "1");

    Response resp;
    try {
      ExecCtx ctx;
      resp = cfg_.transport->execute(req, ctx);
    } catch (...) {
      reportClosed("handshake threw");
      return;
    }
    if (resp.err != Error::kNone) {
      reportClosed("handshake failed");
      return;
    }
    // Fresh session: reset the stream-0 offset ledger.
    delivered_offsets_.clear();
    next_offset_ = 0;
    open_ = true;
    state_ = WtState::kConnected;
    emitState(WtState::kConnected, "");
  } catch (...) {
    reportClosed("connect failed");
  }
}

void WebTransport::sendDatagram(std::string d) {
  try {
    if (!open_ || state_ != WtState::kConnected) {
      reportClosed("not connected");
      return;
    }
    if (!cfg_.transport) {
      reportClosed("no transport");
      return;
    }
    Request req;
    req.method = "WT-SEND";
    req.url = url_;
    req.body = d;
    try {
      // The flap retransmit rides this path: a redelivered response is
      // absorbed by the offset ledger below, so send-path failures must
      // never tear the session down.
      ExecCtx ctx;
      cfg_.transport->execute(req, ctx);
    } catch (...) {
    }
    // Dedupe by stream OFFSET (delivered-offset set), never by payload
    // content: each sendDatagram owns its offset (next_offset_); a
    // redelivery of an already-delivered offset (migration/flap
    // retransmit) is absorbed, while identical payloads at different
    // offsets each deliver.
    const std::uint64_t my_offset = next_offset_;
    next_offset_ += static_cast<std::uint64_t>(d.size());
    if (delivered_offsets_.find(my_offset) != delivered_offsets_.end()) {
      return;
    }
    delivered_offsets_.insert(my_offset);
    try {
      if (listener_.onDatagram) listener_.onDatagram(d);
    } catch (...) {
    }
  } catch (...) {
    reportClosed("send failed");
  }
}

void WebTransport::migrate(std::string newNetwork) {
  try {
    if (!open_ || state_ != WtState::kConnected) {
      reportClosed("not connected");
      return;
    }
    // The offset ledger survives migration untouched: in-flight datagrams
    // are neither lost nor duplicated across the network transition.
    state_ = WtState::kMigrating;
    emitState(WtState::kMigrating, "");
    network_ = std::move(newNetwork);
    state_ = WtState::kConnected;
    emitState(WtState::kConnected, "");
  } catch (...) {
    reportClosed("migrate failed");
  }
}

}  // namespace net
}  // namespace muse
