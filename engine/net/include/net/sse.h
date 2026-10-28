#pragma once

// Server-sent events client over the ITransport seam
// (FR-NET-009 / AC-NET-017 SSE half).
//
// The SSE wire body is `event:<e>\ndata:<d>\nid:<id>\n\n` frames; the
// client parses event/data/id and invokes the callback once per frame, in
// body order. Malformed lines (no colon) are skipped; a block without a
// data line dispatches nothing. Every failure (null transport, empty url,
// transport error/throw) delivers no events and never escapes as an
// exception. cancel() is safe to call from inside the callback and any
// number of times.

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "net/transport.h"
#include "net/types.h"

namespace muse {
namespace net {

class SseClient {
 public:
  using EventCallback =
      std::function<void(std::string event, std::string data, std::string id)>;

  SseClient();
  explicit SseClient(std::shared_ptr<ITransport> transport);

  void setTransport(std::shared_ptr<ITransport> transport);
  void setLastEventId(std::string id);

  void subscribe(std::string url, EventCallback onEvent);
  void cancel();

 private:
  std::shared_ptr<ITransport> transport_;
  std::string last_event_id_;
  std::atomic<bool> cancelled_{false};
};

namespace detail {

// One parsed SSE frame: (event, data, id).
using SseFrame = std::tuple<std::string, std::string, std::string>;

// Parses an SSE body into frames in body order. Never throws.
std::vector<SseFrame> ParseSseFrames(const std::string& body) noexcept;

}  // namespace detail

}  // namespace net
}  // namespace muse
