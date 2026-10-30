// FR-TEXT-016 / AC-TEXT-045: glyph-atlas cache implementation.
// LRU over an ordered key map with a monotonic recency tick; eviction
// scans for the stalest tick (caps are small, worst case is flat).

#include "muse/text/glyph_atlas.h"

#include <cstring>

namespace muse::text {

GlyphAtlas::Key GlyphAtlas::MakeKey(FontHandle font, float scale,
                                    std::uint32_t glyph_id) {
  Key key;
  key.font_id = font.id;
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "scale key needs 32-bit float bits");
  std::uint32_t bits = 0;
  std::memcpy(&bits, &scale, sizeof(bits));
  key.scale_bits = bits;
  key.glyph_id = glyph_id;
  return key;
}

GlyphAtlas::GlyphAtlas() : GlyphAtlas(kDefaultMaxEntries, kDefaultMaxBytes) {}

GlyphAtlas::GlyphAtlas(std::size_t max_entries, std::size_t max_bytes)
    : max_entries_(max_entries), max_bytes_(max_bytes) {}

bool GlyphAtlas::KeyLess::operator()(const Key& a, const Key& b) const {
  if (a.font_id != b.font_id) return a.font_id < b.font_id;
  if (a.scale_bits != b.scale_bits) return a.scale_bits < b.scale_bits;
  return a.glyph_id < b.glyph_id;
}

bool GlyphAtlas::Fits(std::size_t entries) const {
  return entries <= max_entries_ && entries * kEntryBytes <= max_bytes_;
}

void GlyphAtlas::EvictLru() {
  if (entries_.empty()) return;
  auto victim = entries_.begin();
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->second.last_use < victim->second.last_use) victim = it;
  }
  entries_.erase(victim);
}

std::optional<AtlasEntry> GlyphAtlas::lookup(FontHandle font, float scale,
                                             std::uint32_t glyph_id) {
  const auto it = entries_.find(MakeKey(font, scale, glyph_id));
  if (it == entries_.end()) {
    ++misses_;
    return std::nullopt;
  }
  ++hits_;
  it->second.last_use = ++tick_;
  AtlasEntry entry;
  entry.texels = it->second.texels;
  return entry;
}

Status GlyphAtlas::insert(FontHandle font, float scale,
                          std::uint32_t glyph_id,
                          const TexelRecord& texels) {
  const Key key = MakeKey(font, scale, glyph_id);
  const auto it = entries_.find(key);
  if (it != entries_.end()) {
    // Refresh in place: no residency change, no eviction.
    it->second.texels = texels;
    it->second.last_use = ++tick_;
    return Status::kOk;
  }
  if (Fits(entries_.size() + 1)) {
    Slot slot;
    slot.texels = texels;
    slot.last_use = ++tick_;
    entries_.emplace(key, slot);
    return Status::kOk;
  }
  // Bound breach: evict LRU, then retry once. A second breach reports
  // kCacheFull and the cache never grows past its caps.
  EvictLru();
  if (Fits(entries_.size() + 1)) {
    Slot slot;
    slot.texels = texels;
    slot.last_use = ++tick_;
    entries_.emplace(key, slot);
    return Status::kOk;
  }
  return Status::kCacheFull;
}

GlyphAtlasStats GlyphAtlas::stats() const {
  GlyphAtlasStats out;
  out.hits = hits_;
  out.misses = misses_;
  out.entries = entries_.size();
  out.bytes = entries_.size() * kEntryBytes;
  return out;
}

}  // namespace muse::text
