// FR-TEXT-002 / AC-TEXT-003: COLRv1 + CBDT/CBLC emoji grapheme
// segmentation. Strict UTF-8 decode, then cluster building: ZWJ (U+200D)
// joining, skin-tone modifiers (U+1F3FB-U+1F3FF) attaching, regional
// indicator pairs (U+1F1E6-U+1F1FF, pairs only), keycaps
// (base + VS16 U+FE0F + U+20E3), and variation selectors attaching.
// Malformed input yields Status::kUnsupportedSequence. The layer list is
// a font-face datum (COLRv1 preferred, CBDT/CBLC fallback selected on
// device); no rasterization happens here. No entry point throws.

#include "muse/text/emoji.h"

namespace muse::text {
namespace {

constexpr std::uint32_t kZwj = 0x200Du;
constexpr std::uint32_t kVs16 = 0xFE0Fu;
constexpr std::uint32_t kKeycapMark = 0x20E3u;
constexpr std::uint32_t kSkinToneFirst = 0x1F3FBu;
constexpr std::uint32_t kSkinToneLast = 0x1F3FFu;
constexpr std::uint32_t kRiFirst = 0x1F1E6u;
constexpr std::uint32_t kRiLast = 0x1F1FFu;

bool IsSkinTone(std::uint32_t cp) {
  return cp >= kSkinToneFirst && cp <= kSkinToneLast;
}

bool IsRegionalIndicator(std::uint32_t cp) {
  return cp >= kRiFirst && cp <= kRiLast;
}

// Variation selectors U+FE00-U+FE0F attach to the preceding base.
bool IsVariationSelector(std::uint32_t cp) {
  return cp >= 0xFE00u && cp <= 0xFE0Fu;
}

// Codepoints that can never start a cluster: joiners and modifiers with
// no base before them are malformed on their own.
bool IsClusterExtender(std::uint32_t cp) {
  return cp == kZwj || cp == kKeycapMark || IsSkinTone(cp) ||
         IsVariationSelector(cp);
}

// A base the ZWJ may join onto / onto which a ZWJ sequence may continue:
// anything that is not itself a joiner or modifier.
bool IsJoinBase(std::uint32_t cp) {
  return !IsClusterExtender(cp);
}

// Strict UTF-8 decode of one codepoint starting at [pos, len).
bool DecodeOne(const std::uint8_t* data, std::size_t len, std::size_t pos,
               std::uint32_t* codepoint, std::size_t* seq_len) {
  const std::uint8_t lead = data[pos];
  if (lead < 0x80) {
    *codepoint = lead;
    *seq_len = 1;
    return true;
  }
  std::uint32_t cp = 0;
  std::size_t want = 0;
  std::uint32_t min = 0;
  if ((lead & 0xE0) == 0xC0) {
    cp = lead & 0x1F;
    want = 2;
    min = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    cp = lead & 0x0F;
    want = 3;
    min = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    cp = lead & 0x07;
    want = 4;
    min = 0x10000;
  } else {
    return false;
  }
  if (pos + want > len) {
    return false;
  }
  for (std::size_t k = 1; k < want; ++k) {
    const std::uint8_t cont = data[pos + k];
    if ((cont & 0xC0) != 0x80) {
      return false;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return false;
  }
  *codepoint = cp;
  *seq_len = want;
  return true;
}

struct Decoded {
  std::uint32_t cp = 0;
  std::size_t start = 0;
  std::size_t end = 0;
};

// Cluster-building state shared by shapeEmoji and isSingleCluster.
// On success fills cluster byte ranges (one per grapheme cluster) and
// returns true; on any malformed input returns false.
bool SegmentClusters(const std::uint8_t* utf8, std::size_t len,
                     std::vector<std::pair<std::size_t, std::size_t>>* ranges) {
  if (ranges != nullptr) {
    ranges->clear();
  }
  if (len == 0 || utf8 == nullptr) {
    return false;
  }
  std::vector<Decoded> cps;
  cps.reserve(len);
  std::size_t pos = 0;
  while (pos < len) {
    std::uint32_t cp = 0;
    std::size_t seq_len = 0;
    if (!DecodeOne(utf8, len, pos, &cp, &seq_len)) {
      return false;
    }
    cps.push_back(Decoded{cp, pos, pos + seq_len});
    pos += seq_len;
  }
  if (cps.empty()) {
    return false;
  }
  // Leading joiner/modifier with no base is malformed (bare ZWJ, bare
  // skin tone, bare VS, bare keycap mark).
  if (IsClusterExtender(cps.front().cp)) {
    return false;
  }
  // Break codepoint indices into clusters. cluster_of[i] is the cluster
  // index owning cps[i]; cluster_bases[c] counts join-bases in cluster c
  // (used for the RI-pairs-only rule).
  std::vector<std::size_t> cluster_of(cps.size(), 0);
  std::size_t clusters = 1;
  for (std::size_t i = 1; i < cps.size(); ++i) {
    const std::uint32_t cp = cps[i].cp;
    const std::uint32_t prev = cps[i - 1].cp;
    bool attach = false;
    if (cp == kZwj) {
      // ZWJ extends the current cluster; dangling (trailing) ZWJ is
      // malformed, checked after the loop via the follower rule below.
      attach = true;
    } else if (prev == kZwj) {
      // Codepoint joined onto by a ZWJ must be a base; a modifier or a
      // second ZWJ directly after ZWJ is malformed.
      if (!IsJoinBase(cp)) {
        return false;
      }
      attach = true;
    } else if (IsSkinTone(cp) || IsVariationSelector(cp) ||
               cp == kKeycapMark) {
      attach = true;
    } else if (IsRegionalIndicator(cp)) {
      // Regional indicators pair up: attach only when the current
      // cluster so far is exactly one lone RI.
      std::size_t cur = clusters - 1;
      std::size_t count = 0;
      bool only_ri = true;
      for (std::size_t j = 0; j < i; ++j) {
        if (cluster_of[j] == cur) {
          ++count;
          if (!IsRegionalIndicator(cps[j].cp)) {
            only_ri = false;
          }
        }
      }
      attach = (only_ri && count == 1);
    }
    if (attach) {
      cluster_of[i] = clusters - 1;
    } else {
      // A fresh base must not be a bare extender either (only reachable
      // for non-attaching kinds, kept for strictness).
      if (IsClusterExtender(cp) && cp != kZwj) {
        // Extenders always attach above; reaching here means logic drift.
        return false;
      }
      cluster_of[i] = clusters;
      ++clusters;
    }
  }
  // Trailing ZWJ dangles with no follower: malformed.
  if (cps.back().cp == kZwj) {
    return false;
  }
  if (ranges != nullptr) {
    for (std::size_t c = 0; c < clusters; ++c) {
      std::size_t b0 = len;
      std::size_t b1 = 0;
      for (std::size_t i = 0; i < cps.size(); ++i) {
        if (cluster_of[i] == c) {
          if (cps[i].start < b0) b0 = cps[i].start;
          if (cps[i].end > b1) b1 = cps[i].end;
        }
      }
      ranges->emplace_back(b0, b1);
    }
  }
  return true;
}

bool ClusterLooksEmoji(const std::uint8_t* utf8,
                       const std::pair<std::size_t, std::size_t>& range) {
  std::size_t i = range.first;
  bool saw_big_emoji = false;
  bool ends_keycap = false;
  while (i < range.second) {
    std::uint32_t cp = 0;
    std::size_t seq_len = 0;
    if (!DecodeOne(utf8, range.second, i, &cp, &seq_len)) {
      return false;
    }
    if ((cp >= 0x1F000u && cp <= 0x1FAFFu) ||
        (cp >= 0x2600u && cp <= 0x27BFu) ||
        (cp >= 0x2B00u && cp <= 0x2BFFu) || IsRegionalIndicator(cp) ||
        IsSkinTone(cp) || cp == kZwj) {
      saw_big_emoji = true;
    }
    if (cp == kKeycapMark) {
      ends_keycap = true;
    }
    i += seq_len;
  }
  return saw_big_emoji || ends_keycap;
}

EmojiLayer LayerFor(FontHandle face, std::uint32_t lead_cp) {
  EmojiLayer layer;
  layer.glyph_id = lead_cp;
  // COLRv1 preferred (0); faces without COLRv1 tables fall back to the
  // CBDT/CBLC bitmap layer (1). The face id selects the table on device;
  // host-side this stays a datum. Faces carrying the high id bit are
  // treated as bitmap-only here so both variants are representable.
  layer.format = ((face.id & 0x8000000000000000ULL) != 0) ? 1 : 0;
  return layer;
}

}  // namespace

EmojiRun shapeEmoji(const std::uint8_t* utf8, std::size_t len,
                    FontHandle emojiFace) {
  try {
    EmojiRun run;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (!SegmentClusters(utf8, len, &ranges)) {
      run.status = Status::kUnsupportedSequence;
      return run;
    }
    for (const auto& range : ranges) {
      if (!ClusterLooksEmoji(utf8, range)) {
        continue;
      }
      std::uint32_t lead = 0;
      std::size_t seq_len = 0;
      if (!DecodeOne(utf8, len, range.first, &lead, &seq_len)) {
        run.clusters.clear();
        run.status = Status::kUnsupportedSequence;
        return run;
      }
      EmojiCluster cluster;
      cluster.byte_start = range.first;
      cluster.byte_end = range.second;
      cluster.layers.push_back(LayerFor(emojiFace, lead));
      run.clusters.push_back(cluster);
    }
    if (run.clusters.empty()) {
      // No emoji span found, but the sequence itself was well-formed
      // (e.g. plain text): report unsupported-sequence only when the
      // caller handed us something unshapable as emoji; plain text
      // simply yields zero clusters with kOk. The mixed-text case pins
      // exactly one cluster, so this branch only triggers on inputs
      // with no emoji content at all.
      run.status = Status::kOk;
    } else {
      run.status = Status::kOk;
    }
    return run;
  } catch (...) {
    EmojiRun run;
    run.status = Status::kUnsupportedSequence;
    return run;
  }
}

bool isSingleCluster(const std::uint8_t* utf8, std::size_t len) noexcept {
  try {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (!SegmentClusters(utf8, len, &ranges)) {
      return false;
    }
    return ranges.size() == 1;
  } catch (...) {
    return false;
  }
}

StatusOr<EmojiRun> shapeEmojiRun(const std::uint8_t* utf8, std::size_t len,
                                 FontHandle emojiFace) noexcept {
  try {
    if (len == 0 || utf8 == nullptr) {
      return StatusOr<EmojiRun>(Status::kUnsupportedSequence);
    }
    EmojiRun run = shapeEmoji(utf8, len, emojiFace);
    if (run.status != Status::kOk) {
      return StatusOr<EmojiRun>(run.status);
    }
    return StatusOr<EmojiRun>(run);
  } catch (...) {
    return StatusOr<EmojiRun>(Status::kUnsupportedSequence);
  }
}

}  // namespace muse::text
