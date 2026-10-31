// T1 implementation — see engine/media/include/media/image_cache.h.
#include "engine/media/include/media/image_cache.h"

#include <limits>

namespace media {

ImageCache::ImageCache()
    : budgetBytes_((std::numeric_limits<std::size_t>::max)()) {}

void ImageCache::evictMemory() noexcept { memory_.clear(); }

void ImageCache::setBudgetBytes(std::size_t bytes) noexcept {
  budgetBytes_ = bytes;
}

void ImageCache::setDecodeCaps(DecodeCaps caps) noexcept { caps_ = caps; }

DecodeCaps ImageCache::decodeCaps() const noexcept { return caps_; }

CacheResult imageCacheLoad(ImageCache& c, std::string url) {
  try {
    if (url.empty()) {
      return CacheResult{HitTier::kMiss, false, Status::kInvalidArg};
    }
    if (c.memory_.find(url) != c.memory_.end()) {
      return CacheResult{HitTier::kMemory, false, Status::kOk};
    }
    const bool onDisk = c.disk_.find(url) != c.disk_.end();
    if (onDisk) {
      // No re-decode: repromote into memory. Repromotion is best-effort
      // under a refused budget — the disk hit itself still succeeds.
      if (c.budgetBytes_ > 0) {
        c.memory_.insert(url);
      }
      return CacheResult{HitTier::kDisk, false, Status::kOk};
    }
    // Miss: admit through the budget seam (`acquireVram` stand-in).
    // Upload branch (DecodeCaps::hostImageCopy ? direct poolAcquire
    // admission : staged fallback) admits identically in T1; the
    // zero-copy trace is asserted by T5 integration.
    (void)c.caps_;
    if (c.budgetBytes_ == 0) {
      return CacheResult{HitTier::kMiss, false, Status::kOverBudget};
    }
    c.disk_.insert(url);
    c.memory_.insert(url);
    ++c.decodeCount;
    return CacheResult{HitTier::kMiss, true, Status::kOk};
  } catch (...) {
    return CacheResult{HitTier::kMiss, false, Status::kOverBudget};
  }
}

}  // namespace media
