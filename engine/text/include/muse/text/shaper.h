#pragma once

// FR-TEXT-007 / AC-TEXT-010: Zero-JNI C++ shaping entry points.
// The per-frame path takes plain buffers and value structs only — no
// JNIEnv*, JavaVM, or any other JVM/JNI type appears in these signatures.
// Real HarfBuzz/ICU backends plug in behind shapeRun/measureLine in a
// follow-up; this core owns the pipeline structure (UTF-8 decode ->
// cluster map -> advances -> measure) with a deterministic stub backend.

#include <cstddef>
#include <cstdint>

#include "muse/text/text_types.h"

namespace muse::text {

// Shapes a UTF-8 run into glyph ids, advances, offsets, and a per-byte
// cluster map. Empty input (len == 0) is a valid empty run. Malformed or
// truncated UTF-8 yields Status::kInvalidScript; never throws.
StatusOr<ShapedRun> shapeRun(const std::uint8_t* utf8, std::size_t len,
                             FontHandle font, const TextStyle& style);

// Measures a shaped run in C++. The returned width never exceeds
// maxWidthPx (negative budgets clamp to 0).
MeasuredLine measureLine(ShapedRunView run, float maxWidthPx);

}  // namespace muse::text
