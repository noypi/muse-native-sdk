// T3 — Glyph-atlas cache keyed per font/scale with bound + eviction.
// FR-TEXT-016 / AC-TEXT-045 (atlas cached per font/scale, reused across
// frames); Q-TEXT-005 (stated entry + byte cap + LRU eviction).
// Hermetic: C++17, no framework, no device, no downloads, no config.
//
// Binds to the plan T3 `Interface:` lines verbatim (see unit-tests/T3.md).
// ASSUMPTION (location): `muse/text/glyph_atlas.h` declares
//   `muse::text::GlyphAtlas` alongside `AtlasEntry` / `TexelRecord`.
// ASSUMPTION (construction): `GlyphAtlas(size_t maxEntries, size_t maxBytes)`
//   states the entry cap + byte cap; a default-constructed atlas carries
//   sane production defaults (not asserted here).
// ASSUMPTION (scale key): the `scale` parameter carries the
//   `Density::fontScale` dimension, so distinct gallery font scales
//   (1.0 / 1.3 / 2.0) are distinct keys.
// ASSUMPTION (records): `TexelRecord` is default-constructible and each
//   inserted record contributes a fixed positive byte cost (coder-defined);
//   only relative bounds and the `kCacheFull` signal are asserted.
// ASSUMPTION (status): `muse::text::Status::kCacheFull` is the bound-breach
//   signal returned after evict-LRU-then-retry-once fails a second time.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "muse/text/glyph_atlas.h"

namespace {

constexpr std::size_t kBigBytes = 1u << 20;

// Miss on an empty cache; miss counter moves, hit counter does not.
static bool check_lookup_on_empty_is_miss() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  const auto hit =
      atlas.lookup(muse::text::FontHandle(1), 1.0f, 42u);
  if (hit.has_value()) return false;
  const auto s = atlas.stats();
  return s.hits == 0u && s.misses == 1u && s.entries == 0u;
}

// Insert then lookup the same (font, scale, glyph) hits.
static bool check_insert_then_lookup_hits() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 42u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  const auto hit =
      atlas.lookup(muse::text::FontHandle(1), 1.0f, 42u);
  if (!hit.has_value()) return false;
  const auto s = atlas.stats();
  return s.hits == 1u && s.entries == 1u && s.bytes > 0u;
}

// Repeated lookups across frames hit: one entry, N hits (AC-TEXT-045
// "reused across frames").
static bool check_repeated_lookups_across_frames_hit() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(3), 1.3f, 7u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  for (int frame = 0; frame < 3; ++frame) {
    if (!atlas.lookup(muse::text::FontHandle(3), 1.3f, 7u).has_value()) {
      return false;
    }
  }
  const auto s = atlas.stats();
  return s.hits == 3u && s.misses == 0u && s.entries == 1u;
}

// Gallery font scales are distinct keys: 1.0 vs 1.3 vs 2.0.
static bool check_distinct_scales_are_distinct_entries() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 9u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  if (atlas.lookup(muse::text::FontHandle(1), 1.3f, 9u).has_value()) {
    return false;
  }
  if (atlas.lookup(muse::text::FontHandle(1), 2.0f, 9u).has_value()) {
    return false;
  }
  return atlas.lookup(muse::text::FontHandle(1), 1.0f, 9u).has_value();
}

// Different fonts never share an entry.
static bool check_distinct_fonts_are_misses() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 9u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  return !atlas.lookup(muse::text::FontHandle(2), 1.0f, 9u).has_value();
}

// Different glyph ids never share an entry.
static bool check_distinct_glyphs_are_misses() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 9u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  return !atlas.lookup(muse::text::FontHandle(1), 1.0f, 10u).has_value();
}

// Entry cap evicts LRU: filling a 2-entry cache, touching A, then inserting
// C keeps A + C and drops B; insert succeeds after the single retry.
static bool check_entry_cap_evicts_lru() {
  muse::text::GlyphAtlas atlas(2, kBigBytes);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 1u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 2u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  // Touch glyph 1 so glyph 2 is the LRU victim.
  if (!atlas.lookup(muse::text::FontHandle(1), 1.0f, 1u).has_value()) {
    return false;
  }
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 3u, texels) !=
      muse::text::Status::kOk) {
    return false;
  }
  const auto s = atlas.stats();
  if (s.entries != 2u) return false;
  if (!atlas.lookup(muse::text::FontHandle(1), 1.0f, 1u).has_value()) {
    return false;
  }
  if (!atlas.lookup(muse::text::FontHandle(1), 1.0f, 3u).has_value()) {
    return false;
  }
  return !atlas.lookup(muse::text::FontHandle(1), 1.0f, 2u).has_value();
}

// Insert storm stays bounded: 10x the entry cap of distinct inserts never
// grows the cache past the stated caps (Q-TEXT-005 / AC-PERFORMANCE-008).
static bool check_insert_storm_stays_bounded() {
  muse::text::GlyphAtlas atlas(8, kBigBytes);
  muse::text::TexelRecord texels;
  for (std::uint32_t g = 0; g < 80; ++g) {
    const auto st =
        atlas.insert(muse::text::FontHandle(1), 1.0f, g, texels);
    if (st != muse::text::Status::kOk &&
        st != muse::text::Status::kCacheFull) {
      return false;
    }
    const auto s = atlas.stats();
    if (s.entries > 8u || s.bytes > kBigBytes) return false;
  }
  const auto s = atlas.stats();
  return s.entries <= 8u && s.bytes <= kBigBytes;
}

// Zero byte cap fits nothing: insert evicts, retries once, then reports
// kCacheFull without growing.
static bool check_zero_byte_cap_reports_cache_full() {
  muse::text::GlyphAtlas atlas(8, 0u);
  muse::text::TexelRecord texels;
  if (atlas.insert(muse::text::FontHandle(1), 1.0f, 1u, texels) !=
      muse::text::Status::kCacheFull) {
    return false;
  }
  const auto s = atlas.stats();
  return s.entries == 0u && s.bytes == 0u;
}

}  // namespace

#define RUN_CASE(name)                        \
  do {                                        \
    if (!(name)()) {                          \
      std::printf("FAIL %s\n", #name);        \
      ++failures;                             \
    } else {                                  \
      std::printf("PASS %s\n", #name);        \
    }                                         \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_lookup_on_empty_is_miss);
  RUN_CASE(check_insert_then_lookup_hits);
  RUN_CASE(check_repeated_lookups_across_frames_hit);
  RUN_CASE(check_distinct_scales_are_distinct_entries);
  RUN_CASE(check_distinct_fonts_are_misses);
  RUN_CASE(check_distinct_glyphs_are_misses);
  RUN_CASE(check_entry_cap_evicts_lru);
  RUN_CASE(check_insert_storm_stays_bounded);
  RUN_CASE(check_zero_byte_cap_reports_cache_full);
  if (failures > 0) {
    std::printf("glyph_atlas_test: %d failing\n", failures);
    return 1;
  }
  std::printf("glyph_atlas_test: all passing\n");
  return 0;
}
