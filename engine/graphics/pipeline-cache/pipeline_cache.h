#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace graphics {

// Key identifying one pipeline-cache blob. Both versions participate:
// any mismatch discards the stored blob and rebuilds.
struct CacheKey {
  std::string driver_version;
  std::string engine_version;
};

// Handle to an opened cache entry. `valid()` means the blob is usable;
// `cache_hit()` is true iff the blob was reused from a previous open;
// `blob_bytes()` is the (non-empty) rebuilt or reused blob size.
class CacheHandle {
 public:
  CacheHandle(bool valid, bool cache_hit, std::size_t blob_bytes)
      : valid_(valid), cache_hit_(cache_hit), blob_bytes_(blob_bytes) {}
  bool valid() const { return valid_; }
  bool cache_hit() const { return cache_hit_; }
  std::size_t blob_bytes() const { return blob_bytes_; }

 private:
  bool valid_;
  bool cache_hit_;
  std::size_t blob_bytes_;
};

// Per-frame shader/pipeline compile observation.
struct FrameTrace {
  uint32_t compile_stalls = 0;
};

// Test-only seam: selects the persistence directory (tests point it at a
// test-owned /tmp path). Resets any previously persisted test blobs so each
// test starts from a clean slate; production behavior is unchanged.
void set_cache_dir_for_test(const std::string& dir);

// Opens (or rebuilds) the cache entry for `key`. A stored blob is served
// only on an exact driver+engine match; any mismatch discards and rebuilds.
CacheHandle open_cache(const CacheKey& key);

// Observes the most recent frame. Zero compile stalls once the cache is
// warm (last open was a hit).
FrameTrace record_frame_trace();

}  // namespace graphics
