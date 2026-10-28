#include "lists/collection_announce.h"

namespace lists {

std::optional<Announcement> MaybeAnnounce(AnnounceState& s, int64_t nowMs,
                                           int leadingIndex, int total) {
  if (!s.hasEmitted) {
    s.hasEmitted = true;
    s.lastEmitMs = nowMs;
    return Announcement{leadingIndex, total};
  }
  if (nowMs - s.lastEmitMs >= kAnnounceThrottleMs) {
    s.lastEmitMs = nowMs;
    return Announcement{leadingIndex, total};
  }
  return std::nullopt;
}

}  // namespace lists
