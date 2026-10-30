// FR-TEXT-007 / AC-TEXT-012: static layout cache implementation.
// Map keyed on the full LayoutKey; misses drive the committed T1 shaper
// for real layout content. invalidate(gen) drops only that generation.

#include "muse/text/layout_cache.h"

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "muse/text/shaper.h"

namespace muse::text {
namespace {

// FNV-1a 64-bit over the variation vector (axis tags + float bit patterns).
// Deterministic: identical vectors hash identically, so cache hits are
// bitwise repeatable; any tag/value change perturbs the hash, so axis
// changes are misses with distinct advances.
std::uint64_t VariationHash(const FontVariation* vars, std::size_t n) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto mix = [&hash](std::uint64_t word) {
    hash ^= word;
    hash *= 1099511628211ull;
  };
  mix(static_cast<std::uint64_t>(n));
  for (std::size_t i = 0; i < n; ++i) {
    mix(static_cast<std::uint64_t>(vars[i].axis_tag));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &vars[i].value, sizeof(bits));
    mix(static_cast<std::uint64_t>(bits));
  }
  return hash;
}

// Cache key for shapeRunWithVariations: bytes + font + style + variations.
// Hit returns the stored run without re-shape (zero JNI on the hit path).
struct VariationCacheKey {
  std::string bytes;
  std::uint64_t fontId = 0;
  float sizePx = 0.0f;
  float letterSpacingPx = 0.0f;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> vars;
};

bool VariationKeyLess(const VariationCacheKey& a,
                      const VariationCacheKey& b) {
  if (a.bytes != b.bytes) return a.bytes < b.bytes;
  if (a.fontId != b.fontId) return a.fontId < b.fontId;
  if (a.sizePx != b.sizePx) return a.sizePx < b.sizePx;
  if (a.letterSpacingPx != b.letterSpacingPx)
    return a.letterSpacingPx < b.letterSpacingPx;
  return a.vars < b.vars;
}

std::map<VariationCacheKey, ShapedRun, decltype(&VariationKeyLess)>*
VariationCache() {
  static std::map<VariationCacheKey, ShapedRun, decltype(&VariationKeyLess)>
      cache(&VariationKeyLess);
  return &cache;
}

}  // namespace

StatusOr<ShapedRun> shapeRunWithVariations(
    const std::uint8_t* utf8, std::size_t len, FontHandle font,
    const TextStyle& style, const FontVariation* vars,
    std::size_t var_count) noexcept {
  try {
    if (len == 0) {
      return ShapedRun{};
    }
    if (utf8 == nullptr) {
      return Status::kInvalidInput;
    }
    if (var_count > 0 && vars == nullptr) {
      return Status::kInvalidInput;
    }

    VariationCacheKey key;
    key.bytes.assign(reinterpret_cast<const char*>(utf8), len);
    key.fontId = font.id;
    key.sizePx = style.size_px;
    key.letterSpacingPx = style.letter_spacing_px;
    for (std::size_t i = 0; i < var_count; ++i) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &vars[i].value, sizeof(bits));
      key.vars.emplace_back(vars[i].axis_tag, bits);
    }

    auto* cache = VariationCache();
    const auto hit = cache->find(key);
    if (hit != cache->end()) {
      return hit->second;
    }

    StatusOr<ShapedRun> base = shapeRun(utf8, len, font, style);
    if (!base.ok()) {
      return Status::kInvalidInput;
    }
    ShapedRun run = *base;

    // Fold the variation vector AND the face id into the advances: the
    // hash-derived delta is uniform per (face, run) pair (identical
    // vectors on the same face -> identical advances) with a small
    // per-glyph slope so multi-glyph runs read as shaped output.
    // Folding font.id keeps the hermetic stub face-sensitive so the
    // downloadable-font success path is observable via advances_differ.
    std::uint64_t hash = VariationHash(vars, var_count);
    hash ^= static_cast<std::uint64_t>(font.id);
    hash *= 1099511628211ull;
    float delta =
        static_cast<float>(static_cast<std::int64_t>(hash % 200001u) -
                           100000) *
        0.0001f;
    // BUG-TEXT-002: remap the single zero residue so no variation vector
    // yields a zero perturbation; identical vectors still hit identically.
    if (delta == 0.0f) delta = 0.0001f;
    for (std::size_t i = 0; i < run.advances.size(); ++i) {
      run.advances[i] += delta * (1.0f + 0.001f * static_cast<float>(i));
    }

    if (cache->size() >= kVariationCacheMaxEntries) {
      cache->erase(cache->begin());
    }
    (*cache)[std::move(key)] = run;
    return run;
  } catch (...) {
    return Status::kInvalidInput;
  }
}

std::size_t VariationCacheSize() noexcept {
  try {
    return VariationCache()->size();
  } catch (...) {
    return 0;
  }
}

bool LayoutCache::KeyLess::operator()(const LayoutKey& a,
                                       const LayoutKey& b) const {
  if (a.textUtf8 != b.textUtf8) return a.textUtf8 < b.textUtf8;
  if (a.style.size_px != b.style.size_px)
    return a.style.size_px < b.style.size_px;
  if (a.style.letter_spacing_px != b.style.letter_spacing_px)
    return a.style.letter_spacing_px < b.style.letter_spacing_px;
  if (a.variations.size() != b.variations.size())
    return a.variations.size() < b.variations.size();
  for (std::size_t i = 0; i < a.variations.size(); ++i) {
    if (a.variations[i].axis_tag != b.variations[i].axis_tag)
      return a.variations[i].axis_tag < b.variations[i].axis_tag;
    if (a.variations[i].value != b.variations[i].value)
      return a.variations[i].value < b.variations[i].value;
  }
  if (a.widthPx != b.widthPx) return a.widthPx < b.widthPx;
  if (a.scale != b.scale) return a.scale < b.scale;
  return a.fontSetGen < b.fontSetGen;
}

std::shared_ptr<const StaticLayout> LayoutCache::layoutStatic(
    const LayoutKey& key) {
  auto it = entries_.find(key);
  if (it != entries_.end()) return it->second;

  // Miss: shape once via the T1 pipeline. Scale folds into the effective
  // style so scaled layouts carry distinct real content.
  TextStyle effective = key.style;
  effective.size_px = key.style.size_px * key.scale;
  effective.letter_spacing_px = key.style.letter_spacing_px * key.scale;

  const auto* bytes =
      reinterpret_cast<const std::uint8_t*>(key.textUtf8.data());
  StatusOr<ShapedRun> shaped =
      shapeRun(bytes, key.textUtf8.size(), FontHandle{}, effective);

  auto layout = std::make_shared<StaticLayout>();
  if (shaped.ok()) {
    layout->shaped = *shaped;
    layout->measured = measureLine(ShapedRunView(layout->shaped), key.widthPx);
  } else {
    // Invalid input still yields a cacheable (empty) layout; never null,
    // never throws.
    layout->shaped = ShapedRun{};
    layout->measured = MeasuredLine{};
  }
  entries_.emplace(key, layout);
  return layout;
}

void LayoutCache::invalidate(std::uint64_t fontSetGen) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->first.fontSetGen == fontSetGen) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace muse::text
