// VRAM trim-tier mapper (NFR-GRAPHICS-004, AC-GRAPHICS-008 scripted-trim half).
#include "engine/graphics/memory/vram_trim.h"

TrimOutcome applyTrim(VramState& state, TrimLevel level) noexcept {
  if (state.usedBytes < 0 || state.visibleBytes < 0 ||
      state.visibleBytes > state.usedBytes) {
    TrimOutcome out;
    out.flushedBytes = 0;
    out.prefetchEnabled = state.prefetchEnabled;
    out.status = Status::kInvalidArg;
    return out;
  }
  const int nonVisible = state.usedBytes - state.visibleBytes;
  int pct = 0;
  switch (level) {
    case TrimLevel::kRunningLow:
      pct = 25;
      break;
    case TrimLevel::kRunningCritical:
      pct = 50;
      break;
    case TrimLevel::kBackground:
      pct = 100;
      break;
  }
  const long long flushedWide =
      static_cast<long long>(nonVisible) * pct / 100;
  int flushed = static_cast<int>(flushedWide);
  if (flushed < 0) flushed = 0;
  if (flushed > nonVisible) flushed = nonVisible;
  state.usedBytes -= flushed;
  if (level == TrimLevel::kRunningLow) {
    state.prefetchEnabled = false;
  }
  TrimOutcome out;
  out.flushedBytes = flushed;
  out.prefetchEnabled = state.prefetchEnabled;
  out.status = Status::kOk;
  return out;
}

bool withinProfileBound(int usedBytes, size_t ramBytes) noexcept {
  if (usedBytes < 0) return false;
  const size_t kMiB = 1024ULL * 1024ULL;
  const size_t k4G = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t k6G = 6ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t kLow = 48ULL * kMiB;
  const size_t kHigh = 128ULL * kMiB;
  size_t bound = kLow;
  if (ramBytes >= k6G) {
    bound = kHigh;
  } else if (ramBytes >= k4G) {
    const size_t kSpan = k6G - k4G;
    const size_t kDelta = kHigh - kLow;
    bound = kLow + (ramBytes - k4G) * kDelta / kSpan;
  }
  return static_cast<size_t>(usedBytes) <= bound;
}
