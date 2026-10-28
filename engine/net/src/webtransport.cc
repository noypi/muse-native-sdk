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
    // Fresh session: reset the per-send sequence ledger.
    delivered_seqs_.clear();
    next_seq_ = 0;
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
      // absorbed by the sequence ledger below, so send-path failures must
      // never tear the session down.
      ExecCtx ctx;
      cfg_.transport->execute(req, ctx);
    } catch (...) {
    }
    // Dedupe by per-SEND sequence (delivered-sequence set), never by
    // payload content nor byte offset: each sendDatagram mints the next
    // monotonic sequence number, so even zero-length datagrams own
    // distinct keys and each deliver. A re-presentation of an
    // already-delivered sequence number (migration/flap retransmit) is
    // absorbed, while identical payloads at different sequence numbers
    // each deliver.
    const std::uint64_t my_seq = next_seq_++;
    if (delivered_seqs_.find(my_seq) != delivered_seqs_.end()) {
      return;
    }
    delivered_seqs_.insert(my_seq);
    // Bound the ledger: evict the oldest sequence once past the retention
    // window. Sequences are minted monotonically, so the oldest retained
    // is (my_seq + 1 - size); evicting it keeps size <= kMaxRetainedSeqs.
    if (delivered_seqs_.size() > kMaxRetainedSeqs) {
      delivered_seqs_.erase(my_seq + 1 - delivered_seqs_.size());
    }
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
    // The sequence ledger survives migration untouched: in-flight datagrams
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
