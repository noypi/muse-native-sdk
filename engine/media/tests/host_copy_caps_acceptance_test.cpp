// Acceptance — AC-MEDIA-007 upload-path seam (FR-MEDIA-004, TC-MEDIA-007).
// Binds to the plan T1 `DecodeCaps::hostImageCopy` branch: where
// VK_EXT_host_image_copy is available the cache must admit through the
// direct texture-pool path, else the staged fallback — with identical
// no-re-decode cache behavior in both modes.
//
// Hermetic: C++17 + engine/media/include/media/image_cache.h only; inline
// URLs; no network/env/filesystem/live services. This pins the seam shape
// (caps default + round-trip + mode-independent admission). A true
// zero-intermediate-copy proof needs a GPU upload trace counter, which does
// not exist in-tree (see test-report coverage gap); that half of the clause
// remains integration scope, not asserted here.
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

// Default capability is the staged fallback (hostImageCopy == false).
static void check_default_caps_staged_fallback() {
  media::ImageCache c;
  CHECK("default caps hostImageCopy false",
        c.decodeCaps().hostImageCopy == false);
}

// Capability round-trips through set/get and never throws.
static void check_caps_round_trip() {
  media::ImageCache c;
  bool threw = false;
  try {
    media::DecodeCaps caps;
    caps.hostImageCopy = true;
    c.setDecodeCaps(caps);
  } catch (...) {
    threw = true;
  }
  CHECK("setDecodeCaps never throws", !threw);
  CHECK("caps round-trip hostImageCopy true",
        c.decodeCaps().hostImageCopy == true);
}

// Full miss -> memory-hit -> disk-hit cycle with host-copy available:
// no re-decode on either hit tier.
static void check_host_copy_mode_no_redecode_cycle() {
  media::ImageCache c;
  media::DecodeCaps caps;
  caps.hostImageCopy = true;
  c.setDecodeCaps(caps);
  media::CacheResult r1 = media::imageCacheLoad(c, "https://img.test/hc.png");
  CHECK("host-copy first load kMiss", r1.tier == media::HitTier::kMiss);
  CHECK("host-copy first load decoded", r1.decoded == true);
  CHECK("host-copy first load kOk", r1.status == media::Status::kOk);
  const std::size_t after_first = c.decodeCount;
  media::CacheResult r2 = media::imageCacheLoad(c, "https://img.test/hc.png");
  CHECK("host-copy repeat kMemory", r2.tier == media::HitTier::kMemory);
  CHECK("host-copy memory hit no re-decode", r2.decoded == false);
  CHECK("host-copy decode counter stable", c.decodeCount == after_first);
  c.evictMemory();
  media::CacheResult r3 = media::imageCacheLoad(c, "https://img.test/hc.png");
  CHECK("host-copy post-eviction kDisk", r3.tier == media::HitTier::kDisk);
  CHECK("host-copy disk hit no re-decode", r3.decoded == false);
  CHECK("host-copy disk counter stable", c.decodeCount == after_first);
}

// Admission is mode-independent: the same URL sequence decodes exactly once
// under staged fallback too, so the caps flag cannot change cache behavior.
static void check_modes_agree_on_admission() {
  media::ImageCache staged;
  media::ImageCache direct;
  media::DecodeCaps caps;
  caps.hostImageCopy = true;
  direct.setDecodeCaps(caps);
  (void)media::imageCacheLoad(staged, "https://img.test/m.png");
  (void)media::imageCacheLoad(direct, "https://img.test/m.png");
  CHECK("both modes decode exactly once",
        staged.decodeCount == 1u && direct.decodeCount == 1u);
  media::CacheResult rs = media::imageCacheLoad(staged, "https://img.test/m.png");
  media::CacheResult rd = media::imageCacheLoad(direct, "https://img.test/m.png");
  CHECK("both modes memory-hit", rs.tier == media::HitTier::kMemory &&
                                 rd.tier == media::HitTier::kMemory);
  CHECK("neither mode re-decodes", rs.decoded == false && rd.decoded == false);
}

// Budget refusal holds in host-copy mode as well (fail-safe: refusal, never
// over-admission).
static void check_host_copy_mode_budget_refusal() {
  media::ImageCache c;
  media::DecodeCaps caps;
  caps.hostImageCopy = true;
  c.setDecodeCaps(caps);
  c.setBudgetBytes(0);
  bool threw = false;
  media::CacheResult r{media::HitTier::kMemory, true, media::Status::kOk};
  try {
    r = media::imageCacheLoad(c, "https://img.test/n.png");
  } catch (...) {
    threw = true;
  }
  CHECK("host-copy zero budget never throws", !threw);
  CHECK("host-copy zero budget kOverBudget",
        r.status == media::Status::kOverBudget);
  CHECK("host-copy refusal decodes nothing", r.decoded == false);
}

int main() {
  check_default_caps_staged_fallback();
  check_caps_round_trip();
  check_host_copy_mode_no_redecode_cycle();
  check_modes_agree_on_admission();
  check_host_copy_mode_budget_refusal();
  if (s_failures == 0) {
    std::printf("ALL GREEN host_copy_caps_acceptance_test\n");
    return 0;
  }
  std::printf("%d FAILURES host_copy_caps_acceptance_test\n", s_failures);
  return 1;
}
