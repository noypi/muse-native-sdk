#pragma once

// T1 — image-cache core (FR-MEDIA-004/AC-MEDIA-007, TC-MEDIA-007).
// `muse::media` LRU pair (memory index + disk index) over an inline
// byte-store seam; decode counter proves no re-decode on hits.
//
// Hermetic: C++17, no network/env/filesystem/live-service deps. Budget and
// worker-pool/upload seams are consumed as shapes only (see DecodeCaps and
// setBudgetBytes); T5 wires the real `core::budget::Manager::acquireVram`
// and `core::thread::WorkerPool` internals.

#include <cstddef>
#include <string>
#include <unordered_set>

namespace media {

enum class HitTier { kMiss, kDisk, kMemory };

enum class Status { kOk, kOverBudget, kInvalidArg };

struct CacheResult {
  HitTier tier;
  bool decoded;
  Status status;
};

// Capability query seam (Q-MEDIA-001 evidence, not a device matrix):
// VK_EXT_host_image_copy present -> direct texture-pool admission,
// else staged fallback. T1 admits identically either way; T5 asserts the
// zero-copy trace.
struct DecodeCaps {
  bool hostImageCopy = false;
};

class ImageCache {
 public:
  ImageCache();

  // Incremented exactly once per actual decode (miss admission).
  std::size_t decodeCount = 0;

  // Drops the memory index, keeps the disk index. Never throws.
  void evictMemory() noexcept;

  // Admission budget in bytes. 0 forces kOverBudget refusal (hermetic
  // stand-in for `core::budget::Manager::acquireVram` refusal).
  // Never throws.
  void setBudgetBytes(std::size_t bytes) noexcept;

  // Upload-path capability. Defaults to staged fallback. Never throws.
  void setDecodeCaps(DecodeCaps caps) noexcept;
  DecodeCaps decodeCaps() const noexcept;

 private:
  friend CacheResult imageCacheLoad(ImageCache& c, std::string url);

  std::unordered_set<std::string> memory_;
  std::unordered_set<std::string> disk_;
  std::size_t budgetBytes_;
  DecodeCaps caps_;
};

// Loads `url` through memory -> disk -> decode-and-admit.
//  - empty url -> {kMiss, false, kInvalidArg}
//  - memory hit -> {kMemory, false, kOk} (no re-decode)
//  - disk hit -> {kDisk, false, kOk}, repromotes to memory (no re-decode)
//  - miss with budget -> {kMiss, true, kOk}, admitted to both tiers
//  - miss with zero budget -> {kMiss, false, kOverBudget}
// Never throws.
CacheResult imageCacheLoad(ImageCache& c, std::string url);

}  // namespace media
