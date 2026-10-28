#include "net/sse.h"

#include <utility>

namespace muse {
namespace net {
namespace detail {

std::vector<SseFrame> ParseSseFrames(const std::string& body) noexcept {
  std::vector<SseFrame> frames;
  try {
    std::string event;
    std::string data;
    std::string id;
    bool has_data = false;
    bool has_any_field = false;
    auto dispatch = [&] {
      if (has_data) {
        frames.emplace_back(event.empty() ? "message" : event, data, id);
      }
      event.clear();
      data.clear();
      id.clear();
      has_data = false;
      has_any_field = false;
    };
    std::string line;
    // Walk line by line; a blank line dispatches the current block.
    for (std::size_t i = 0; i <= body.size(); ++i) {
      char c = (i < body.size()) ? body[i] : '\n';
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.empty()) {
          if (has_any_field) dispatch();
          continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
          line.clear();  // Malformed line: skipped, never throws.
          continue;
        }
        std::string field = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        has_any_field = true;
        if (field == "event") {
          event = value;
        } else if (field == "data") {
          if (has_data) data += "\n";
          data += value;
          has_data = true;
        } else if (field == "id") {
          id = value;
        }
        line.clear();
      } else {
        line += c;
      }
    }
    if (has_any_field) dispatch();
  } catch (...) {
    // Parsing never escapes; return what decoded cleanly so far.
  }
  return frames;
}

}  // namespace detail

SseClient::SseClient() = default;

SseClient::SseClient(std::shared_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

void SseClient::setTransport(std::shared_ptr<ITransport> transport) {
  transport_ = std::move(transport);
}

void SseClient::setLastEventId(std::string id) {
  last_event_id_ = std::move(id);
}

void SseClient::subscribe(std::string url, EventCallback onEvent) {
  try {
    cancelled_.store(false);
    if (url.empty() || transport_ == nullptr || !onEvent) return;
    Request r;
    r.method = "GET";
    r.url = std::move(url);
    r.headers.emplace_back("Accept", "text/event-stream");
    if (!last_event_id_.empty()) {
      r.headers.emplace_back("Last-Event-ID", last_event_id_);
    }
    Response resp;
    try {
      resp = transport_->execute(r, ExecCtx{});
    } catch (...) {
      return;  // Transport failure: no events, never throws.
    }
    if (resp.err != Error::kNone) return;
    const std::vector<detail::SseFrame> frames =
        detail::ParseSseFrames(resp.body);
    for (const auto& f : frames) {
      if (cancelled_.load()) break;
      last_event_id_ = std::get<2>(f);
      try {
        onEvent(std::get<0>(f), std::get<1>(f), std::get<2>(f));
      } catch (...) {
        break;  // A failing callback stops delivery; never propagates.
      }
    }
  } catch (...) {
    // subscribe never throws.
  }
}

void SseClient::cancel() { cancelled_.store(true); }

}  // namespace net
}  // namespace muse
