#pragma once

#include <cstdint>
#include <optional>

namespace lists {

// Throttle window for TalkBack position announcements (FR-LISTS-006 /
// AC-LISTS-030). At most one announcement is emitted per window; calls inside
// the window are suppressed. The window is intentionally coarse (half a
// second): a fast fling can traverse hundreds of items per second, and the
// live region must never receive one event per item. The exact ms value is an
// implementation choice documented here; tests pin the collapse behavior
// (fling bursts emit O(1)), not the number itself beyond this constant.
inline constexpr int64_t kAnnounceThrottleMs = 500;

// Throttle state for position announcements. Live-region-safe single pending
// announcement: there is no queue — intermediate positions during a fling are
// overwritten (discarded), and only the latest position is reported when the
// next announcement is allowed. `hasEmitted` is false until the first call,
// so the first call announces immediately; `lastEmitMs` is the timestamp of
// the last emitted announcement.
struct AnnounceState {
  int64_t lastEmitMs = 0;
  bool hasEmitted = false;
};

// A coalesced position announcement: the latest leading index and the total
// item count at emission time.
struct Announcement {
  int leadingIndex = 0;
  int total = 0;
};

// Returns an announcement at most once per `kAnnounceThrottleMs` window,
// carrying the latest position. The first call (no prior emission) announces
// immediately. Calls within the window of the last emission are suppressed
// (return `std::nullopt`), including same-index repeats and fling bursts, so
// a fling over hundreds of items collapses to O(1) announcements. Once
// `nowMs - lastEmitMs >= kAnnounceThrottleMs`, the next call announces again
// with the latest `leadingIndex`/`total`. `nowMs` is caller-supplied
// (explicit clock, hermetic); updates `s` on emission.
std::optional<Announcement> MaybeAnnounce(AnnounceState& s, int64_t nowMs,
                                           int leadingIndex, int total);

}  // namespace lists
