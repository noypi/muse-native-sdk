#pragma once

// FR-TEXT-002 / AC-TEXT-003: color-emoji (COLRv1 + CBDT/CBLC) grapheme
// segmentation over inline UTF-8 byte strings.
//
// shapeEmoji returns one EmojiCluster per grapheme cluster for the emoji
// spans in the input (ZWJ-joined sequences, skin-tone modifiers,
// regional-indicator pairs, and keycap sequences each stay one cluster).
// Non-emoji text contributes no clusters, so mixed text+emoji input
// isolates the emoji span. Each cluster carries a non-empty layer list:
// the COLRv1-vs-CBDT/CBLC raster choice is a font-face/layer-list datum
// on the returned run (graphics owns rasterization; no font files, no
// device, no network here).
//
// Malformed input (strict UTF-8 decode failure, bare/leading/trailing
// ZWJ, dangling joiner or modifier) yields
// `status == Status::kUnsupportedSequence` with empty clusters and
// `isSingleCluster == false`. Valid sequences are never split. No entry
// point throws.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "muse/text/text_types.h"

namespace muse::text {

// One color layer datum for a cluster: COLRv1 layers or the CBDT/CBLC
// bitmap layer, selected per font face on device. Host-side this is an
// opaque datum only (no rasterization here).
struct EmojiLayer {
  std::uint32_t glyph_id = 0;
  // 0 = COLRv1 paint datum, 1 = CBDT/CBLC bitmap datum.
  std::uint8_t format = 0;
};

struct EmojiCluster {
  std::vector<EmojiLayer> layers;
  std::size_t byte_start = 0;
  std::size_t byte_end = 0;
};

struct EmojiRun {
  std::vector<EmojiCluster> clusters;
  Status status = Status::kOk;
};

// Shapes the emoji spans of a UTF-8 buffer into one cluster per grapheme
// cluster with a non-empty layer list. Never throws.
EmojiRun shapeEmoji(const std::uint8_t* utf8, std::size_t len,
                    FontHandle emojiFace);

// Returns true iff the whole input decodes cleanly, passes sequence
// validation, and forms exactly one grapheme cluster. Never throws.
bool isSingleCluster(const std::uint8_t* utf8, std::size_t len) noexcept;

// T4 (FR-TEXT-010 / AC-TEXT-021): StatusOr-returning emoji-run entry point.
// Same clustering/layer semantics as shapeEmoji, but malformed input
// (including null/empty) yields Status::kUnsupportedSequence instead of a
// value run. Never throws; zero JNI.
StatusOr<EmojiRun> shapeEmojiRun(const std::uint8_t* utf8, std::size_t len,
                                 FontHandle emojiFace) noexcept;

}  // namespace muse::text
