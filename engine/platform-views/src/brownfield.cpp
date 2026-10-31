#include "pviews/brownfield.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {

// Monotonic host ids; 0 is reserved as invalid.
std::atomic<std::uint64_t> g_next_host_id{1};
// Host-owned engine ids for multi-mode fallback; offset to avoid colliding
// with embed.cpp's engine ids in practice (tests only need nonzero/distinct).
std::atomic<std::uint64_t> g_next_owned_engine_id{0x40000000ULL};
std::mutex g_mutex;
std::unordered_map<std::uint64_t, std::uint64_t> g_host_to_engine;

bool validKind(HostKind kind) noexcept {
  const int k = static_cast<int>(kind);
  return k >= 0 && k <= 2;
}

bool validMode(HostMode mode) noexcept {
  const int m = static_cast<int>(mode);
  return m >= 0 && m <= 1;
}

}  // namespace

HostCreateResult createHost(HostKind kind, HostMode mode,
                            EngineHandle engine) noexcept {
  if (!validKind(kind) || !validMode(mode)) {
    return HostCreateResult{false, HostHandle{0},
                            HostErrorCode::HOST_BAD_MODE};
  }
  std::uint64_t bound = engine.value;
  if (mode == HostMode::SHARED) {
    if (bound == 0) {
      return HostCreateResult{false, HostHandle{0},
                              HostErrorCode::HOST_NO_ENGINE};
    }
  } else {
    if (bound == 0) {
      bound = g_next_owned_engine_id.fetch_add(1);
    }
  }
  const std::uint64_t id = g_next_host_id.fetch_add(1);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_host_to_engine[id] = bound;
  return HostCreateResult{true, HostHandle{id}, HostErrorCode::NONE};
}

void destroyHost(HostHandle h) noexcept {
  if (h.value == 0) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_host_to_engine.erase(h.value);
}

bool hostIsLive(HostHandle h) noexcept {
  if (h.value == 0) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_host_to_engine.count(h.value) != 0;
}

EngineHandle hostEngineOf(HostHandle h) noexcept {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_host_to_engine.find(h.value);
  if (it == g_host_to_engine.end()) return EngineHandle{0};
  return EngineHandle{it->second};
}

// T7 shared-engine refcount registry (FR-PVIEWS-010 / AC-PVIEWS-014).
// Mint == first holder: an engine minted by embedEngineInView enters with
// count 1. Engines minted by embed.cpp live in that TU, so this registry
// lazy-adopts them via a read-only liveness probe (embedViewInEngine with a
// scratch view); engines unknown to embed.cpp are never adopted — release
// reports ENGINE_DOUBLE_RELEASE and retain returns 0. Fully released ids
// keep a tombstone: no resurrection (retain stays 0) and further releases
// stay ENGINE_DOUBLE_RELEASE. destroyHost never touches this registry and
// release never touches the host map (idempotent destroy interplay).
namespace {
bool engineLiveInEmbed(EngineHandle e) noexcept {
  ViewSlotResult probe =
      embedViewInEngine(e, ViewHandle{0x7FFF0001ULL}, Rect{0, 0, 1, 1});
  return probe.ok;
}
}  // namespace

std::unordered_map<std::uint64_t, int> g_engine_counts;
std::unordered_set<std::uint64_t> g_engine_tombs;

RefCount retainEngine(EngineHandle e) noexcept {
  if (e.value == 0) return 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto t = g_engine_tombs.find(e.value);
    if (t != g_engine_tombs.end()) return 0;  // released: no resurrection
    const auto it = g_engine_counts.find(e.value);
    if (it != g_engine_counts.end()) {
      it->second += 1;
      return it->second;
    }
  }
  if (!engineLiveInEmbed(e)) return 0;  // unknown engine
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_engine_tombs.count(e.value) != 0) return 0;
  auto it = g_engine_counts.find(e.value);
  if (it == g_engine_counts.end()) {
    // Adopt the mint reference, then apply this retain.
    g_engine_counts[e.value] = 2;
    return 2;
  }
  it->second += 1;
  return it->second;
}

EngineReleaseResult releaseOnLastDetach(EngineHandle e) noexcept {
  if (e.value == 0) {
    return EngineReleaseResult{false, 0,
                               EngineTeardownCode::ENGINE_DOUBLE_RELEASE};
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_engine_tombs.count(e.value) != 0) {
      return EngineReleaseResult{false, 0,
                                 EngineTeardownCode::ENGINE_DOUBLE_RELEASE};
    }
    const auto it = g_engine_counts.find(e.value);
    if (it != g_engine_counts.end()) {
      it->second -= 1;
      if (it->second <= 0) {
        g_engine_counts.erase(it);
        g_engine_tombs.insert(e.value);
        return EngineReleaseResult{true, 0,
                                   EngineTeardownCode::RELEASED};
      }
      return EngineReleaseResult{false, it->second,
                                 EngineTeardownCode::STILL_HELD};
    }
  }
  if (!engineLiveInEmbed(e)) {
    return EngineReleaseResult{false, 0,
                               EngineTeardownCode::ENGINE_DOUBLE_RELEASE};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_engine_tombs.count(e.value) != 0) {
    return EngineReleaseResult{false, 0,
                               EngineTeardownCode::ENGINE_DOUBLE_RELEASE};
  }
  const auto it = g_engine_counts.find(e.value);
  if (it == g_engine_counts.end()) {
    // Adopt the mint reference (count 1), then release it: last detach.
    g_engine_tombs.insert(e.value);
    return EngineReleaseResult{true, 0, EngineTeardownCode::RELEASED};
  }
  it->second -= 1;
  if (it->second <= 0) {
    g_engine_counts.erase(it);
    g_engine_tombs.insert(e.value);
    return EngineReleaseResult{true, 0, EngineTeardownCode::RELEASED};
  }
  return EngineReleaseResult{false, it->second,
                             EngineTeardownCode::STILL_HELD};
}
