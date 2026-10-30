#pragma once

// FR-TEXT-016 / AC-TEXT-045: glyph-atlas cache keyed per (font, scale).
// Stores slug_msdf::shapeAndPlace texel records so shaped glyphs are reused
// across frames without re-shape. Text owns selection/positioning only;
// graphics owns atlas allocation/eviction per the SPEC-TYPOGRAPHY-TEXT
// Interfaces table.
//
// Bound: stated entry cap + byte cap with LRU eviction. Insert evicts the
// least-recently-used entry and retries once; a second breach returns
// Status::kCacheFull without growing. This settles Q-TEXT-005 for this
// cache's scope (see AC-PERFORMANCE-008: caches carry stated bounds).

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "muse/text/text_types.h"

namespace muse::text {

// Texel record for one cached glyph: the text-side selection/positioning
// half of the graphics-owned atlas entry (coverage payload lives in
// graphics). Default-constructible; every record carries the same fixed
// positive byte cost so the byte cap is entry-count proportional.
struct TexelRecord {
  std::uint32_t glyph_id = 0;
  std::uint32_t coverage_offset = 0;
  std::uint32_t coverage_size = 0;
  float advance_px = 0.0f;
};

// Value returned by GlyphAtlas::lookup: the cached texels for a key.
struct AtlasEntry {
  TexelRecord texels;
};

// Snapshot of cache counters. hits/misses count lookup() calls; entries and
// bytes describe the current residency (never above the stated caps).
struct GlyphAtlasStats {
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::size_t entries = 0;
  std::size_t bytes = 0;
};

// LRU glyph-atlas cache keyed on (font handle, Density::fontScale dimension
// carried by `scale`, glyph id). Distinct gallery font scales (1.0 / 1.3 /
// 2.0) are distinct keys. Never throws; single-threaded use only.
class GlyphAtlas {
 public:
  // Sane production defaults for a default-constructed atlas (not asserted
  // by unit tests; explicit construction states the test caps).
  static constexpr std::size_t kDefaultMaxEntries = 256;
  static constexpr std::size_t kDefaultMaxBytes = 1u << 20;

  GlyphAtlas();
  GlyphAtlas(std::size_t max_entries, std::size_t max_bytes);

  // Hit returns the cached entry (and marks it most-recently-used); miss
  // returns nullopt and bumps misses. Never throws.
  std::optional<AtlasEntry> lookup(FontHandle font, float scale,
                                   std::uint32_t glyph_id);

  // Insert or replace the record for a key. Breaching either cap evicts the
  // LRU entry and retries once; a second breach returns Status::kCacheFull
  // and the cache does not grow. Never throws.
  Status insert(FontHandle font, float scale, std::uint32_t glyph_id,
                const TexelRecord& texels);

  // Current counters and residency. Never throws.
  GlyphAtlasStats stats() const;

 private:
  struct Key {
    std::uint64_t font_id = 0;
    std::uint32_t scale_bits = 0;
    std::uint32_t glyph_id = 0;
  };
  struct KeyLess {
    bool operator()(const Key& a, const Key& b) const;
  };
  struct Slot {
    TexelRecord texels;
    std::uint64_t last_use = 0;
  };

  static constexpr std::size_t kEntryBytes = sizeof(TexelRecord);

  static Key MakeKey(FontHandle font, float scale, std::uint32_t glyph_id);
  bool Fits(std::size_t entries) const;
  void EvictLru();

  std::size_t max_entries_;
  std::size_t max_bytes_;
  std::uint64_t hits_ = 0;
  std::uint64_t misses_ = 0;
  std::uint64_t tick_ = 0;
  std::map<Key, Slot, KeyLess> entries_;
};

}  // namespace muse::text
