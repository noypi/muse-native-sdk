// Batch 1 — image-cache core (FR-MEDIA-004/AC-MEDIA-007, TC-MEDIA-007).
// Binds ONLY to the T1 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/image_cache.h only;
// no network, no env/config reads, no filesystem, no live services.
// All fixtures are inline URLs.
//
// Expected state: RED — engine/media/include/media/image_cache.h does not
// exist yet, so this file fails to compile until the coder implements T1.
//
// Contract assumptions (see unit-tests/T1.md):
//   A-NS:    everything in namespace `media`; header
//            `engine/media/include/media/image_cache.h`.
//   A-TYPES: `enum class HitTier { kMiss, kDisk, kMemory };`
//            `enum class Status { kOk, kOverBudget, kInvalidArg };`
//            `struct CacheResult { HitTier tier; bool decoded; Status status; };`
//            `CacheResult imageCacheLoad(ImageCache& c, std::string url);`
//   A-CACHE: `media::ImageCache` is default-constructible with ample budget;
//            exposes `std::size_t decodeCount` (incremented once per actual
//            decode); exposes `void evictMemory()` (drops the memory index,
//            keeps the disk index); exposes `void setBudgetBytes(std::size_t)`
//            (0 forces admission refusal with kOverBudget).
#include <cstdio>
#include <string>

#include "engine/media/include/media/image_cache.h"

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// --- cases -----------------------------------------------------------------

// Empty URL is a synchronous kInvalidArg reject that never throws.
static void check_empty_url_rejected() {
  media::ImageCache c;
  bool threw = false;
  media::CacheResult r{media::HitTier::kMiss, true, media::Status::kOk};
  try {
    r = media::imageCacheLoad(c, std::string());
  } catch (...) {
    threw = true;
  }
  CHECK("empty url never throws", !threw);
  CHECK("empty url status kInvalidArg", r.status == media::Status::kInvalidArg);
}

// First load of a URL is a miss that decodes (decoded=true, kOk).
static void check_first_load_miss_decodes() {
  media::ImageCache c;
  media::CacheResult r = media::imageCacheLoad(c, "https://img.test/a.png");
  CHECK("first load tier kMiss", r.tier == media::HitTier::kMiss);
  CHECK("first load decoded", r.decoded == true);
  CHECK("first load status kOk", r.status == media::Status::kOk);
  CHECK("first load bumped decode counter", c.decodeCount == 1u);
}

// Repeated load of the same URL hits memory with no re-decode.
static void check_repeat_load_memory_no_redecode() {
  media::ImageCache c;
  media::CacheResult r1 = media::imageCacheLoad(c, "https://img.test/b.png");
  (void)r1;
  const std::size_t after_first = c.decodeCount;
  media::CacheResult r2 = media::imageCacheLoad(c, "https://img.test/b.png");
  CHECK("repeat load tier kMemory", r2.tier == media::HitTier::kMemory);
  CHECK("memory hit does not re-decode", r2.decoded == false);
  CHECK("memory hit status kOk", r2.status == media::Status::kOk);
  CHECK("decode counter unchanged on memory hit",
        c.decodeCount == after_first);
}

// After memory eviction the URL still hits disk with no re-decode.
static void check_disk_hit_after_memory_eviction() {
  media::ImageCache c;
  media::CacheResult r1 = media::imageCacheLoad(c, "https://img.test/c.png");
  (void)r1;
  const std::size_t after_first = c.decodeCount;
  c.evictMemory();
  media::CacheResult r2 = media::imageCacheLoad(c, "https://img.test/c.png");
  CHECK("post-eviction tier kDisk", r2.tier == media::HitTier::kDisk);
  CHECK("disk hit does not re-decode", r2.decoded == false);
  CHECK("disk hit status kOk", r2.status == media::Status::kOk);
  CHECK("decode counter unchanged on disk hit", c.decodeCount == after_first);
  // And the disk hit repopulates memory: the next load is a memory hit.
  media::CacheResult r3 = media::imageCacheLoad(c, "https://img.test/c.png");
  CHECK("repromoted tier kMemory", r3.tier == media::HitTier::kMemory);
  CHECK("repromoted hit does not re-decode", r3.decoded == false);
}

// Distinct URLs each miss and decode independently.
static void check_distinct_urls_miss_independently() {
  media::ImageCache c;
  media::CacheResult ra =
      media::imageCacheLoad(c, "https://img.test/d1.png");
  media::CacheResult rb =
      media::imageCacheLoad(c, "https://img.test/d2.png");
  CHECK("url A first load kMiss", ra.tier == media::HitTier::kMiss);
  CHECK("url B first load kMiss", rb.tier == media::HitTier::kMiss);
  CHECK("two distinct decodes counted", c.decodeCount == 2u);
}

// Zero budget refuses admission with kOverBudget and never throws.
static void check_over_budget_refused() {
  media::ImageCache c;
  c.setBudgetBytes(0);
  bool threw = false;
  media::CacheResult r{media::HitTier::kMiss, true, media::Status::kOk};
  try {
    r = media::imageCacheLoad(c, "https://img.test/e.png");
  } catch (...) {
    threw = true;
  }
  CHECK("over-budget never throws", !threw);
  CHECK("over-budget status kOverBudget",
        r.status == media::Status::kOverBudget);
}

// Security: malicious/oversized URLs never throw; the result is a
// well-formed shape (kOk hit/miss or a clean rejection).
static void check_hostile_url_never_throws() {
  media::ImageCache c;
  const std::string evil =
      std::string("https://img.test/") + std::string(4096, 'A') +
      std::string("\x00\xff\xfe../..", 8);
  bool threw = false;
  media::CacheResult r{media::HitTier::kMiss, false, media::Status::kOk};
  try {
    r = media::imageCacheLoad(c, evil);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile url never throws", !threw);
  CHECK("hostile outcome is a valid shape",
        (r.status == media::Status::kOk) ||
            (r.status == media::Status::kInvalidArg) ||
            (r.status == media::Status::kOverBudget));
}

int main() {
  check_empty_url_rejected();
  check_first_load_miss_decodes();
  check_repeat_load_memory_no_redecode();
  check_disk_hit_after_memory_eviction();
  check_distinct_urls_miss_independently();
  check_over_budget_refused();
  check_hostile_url_never_throws();
  if (s_failures == 0) {
    std::printf("ALL GREEN image_cache_test\n");
    return 0;
  }
  std::printf("%d FAILURES image_cache_test\n", s_failures);
  return 1;
}
