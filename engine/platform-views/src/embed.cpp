#include "pviews/embed.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {

// Monotonic engine ids; 0 is reserved as invalid.
std::atomic<std::uint64_t> g_next_engine_id{1};
std::mutex g_mutex;
std::unordered_set<std::uint64_t> g_attached_hosts;
std::unordered_set<std::uint64_t> g_live_engines;

}  // namespace

EngineEmbedResult embedEngineInView(HostHandle host, EmbedOpts /*opts*/) noexcept {
  if (host.value == 0) {
    return EngineEmbedResult{false, EngineHandle{0},
                             EmbedErrorCode::EMBED_BAD_HOST};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_attached_hosts.count(host.value) != 0) {
    return EngineEmbedResult{false, EngineHandle{0},
                             EmbedErrorCode::EMBED_ALREADY_ATTACHED};
  }
  const std::uint64_t id = g_next_engine_id.fetch_add(1);
  g_attached_hosts.insert(host.value);
  g_live_engines.insert(id);
  return EngineEmbedResult{true, EngineHandle{id}, EmbedErrorCode::NONE};
}

ViewSlotResult embedViewInEngine(EngineHandle engine, ViewHandle view,
                                 Rect bounds) noexcept {
  if (engine.value == 0 || view.value == 0) {
    return ViewSlotResult{false, ViewSlot{}, EmbedErrorCode::EMBED_BAD_HOST};
  }
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
    return ViewSlotResult{false, ViewSlot{}, EmbedErrorCode::EMBED_BAD_HOST};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_live_engines.count(engine.value) == 0) {
    return ViewSlotResult{false, ViewSlot{}, EmbedErrorCode::EMBED_BAD_HOST};
  }
  ViewSlot slot;
  slot.view = view;
  slot.bounds = bounds;
  return ViewSlotResult{true, slot, EmbedErrorCode::NONE};
}
