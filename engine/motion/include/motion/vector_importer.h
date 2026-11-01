#ifndef MOTION_VECTOR_IMPORTER_H_
#define MOTION_VECTOR_IMPORTER_H_

// VectorImporter importer-only identity (FR-MOTION-017 / AC-MOTION-052).
//
// Converts Lottie/Rive bytes into engine graph nodes at import time (no
// stepping). Importer delegates format gating to the existing
// AssetPlayer::load decoding proxy (motion/asset.h): any non-empty payload
// of printable bytes imports as kOk for its declared Lottie/Rive format;
// empty bytes are unknown-format input (kUnsupported); payloads containing
// non-text bytes are rejected with kDecodeFailed. Nothing here throws;
// malformed input is reported via ImportError, never via an exception.
//
// Per-instance state: each VectorImporter holds its own last-result state;
// no shared mutable import state.
//
// Frame-path guard: runtime stepping of Lottie/Rive frames must never occur
// in the frame path — all conversion happens at import time through
// VectorImporter::import. Any TU that is part of the frame path
// (motion/frame_path.h or the graph evaluate path) defines
// MOTION_VECTOR_RUNTIME_STEP_FROM_FRAME_PATH and calls to
// motion::VectorRuntimeStep() from such a TU fail the build via
// static_assert. NOTE: motion/frame_path.h does not exist in this checkout
// (out-of-claim file, not created here); the guard lives in this header so
// the frame-path evaluate TU can opt in by defining the macro before
// including it.

#include <cstdint>
#include <vector>

#include "motion/asset.h"
#include "motion/graph.h"

namespace motion {

// Plan name for the graph node shape; reuses the graph.h shape per the plan
// Detail ("emit graph nodes reusing graph.h shapes"). Only the spelling is
// reconciled here, never behavior: GraphNode IS the engine Node.
using GraphNode = Node;

}  // namespace motion

// Error shape for VectorImporter::import (mirrors the AssetPlayer decoding
// proxy, narrowed to the importer surface).
enum class ImportError { kOk, kUnsupported, kDecodeFailed };

class VectorImporter {
 public:
  VectorImporter() = default;

  // Imports one Lottie/Rive payload into a graph node at import time (no
  // stepping). Empty input reports kUnsupported via err; undecodable bytes
  // report kDecodeFailed; a decodable Lottie/Rive payload reports kOk and
  // returns the converted node. A format other than kLottie/kRive reports
  // kUnsupported. A null err is tolerated (result still returned). Never
  // throws. Per-instance: records the last result on this instance only.
  motion::GraphNode import(AssetFormat f,
                           const std::vector<uint8_t>& bytes,
                           ImportError* err) noexcept;

 private:
  ImportError last_ = ImportError::kOk;
  unsigned long imports_ = 0;
};

#ifdef MOTION_VECTOR_RUNTIME_STEP_FROM_FRAME_PATH
namespace motion {

// Importer-only constraint: Lottie/Rive runtime must not step frames in the
// frame path — route through VectorImporter. Any call to this function from
// a TU that defines MOTION_VECTOR_RUNTIME_STEP_FROM_FRAME_PATH (the
// frame-path evaluate TU) fails the build. Dependent static_assert so the
// failure fires only when the call is instantiated, not at definition.
template <typename T = void>
inline void VectorRuntimeStep() {
  static_assert(
      sizeof(T*) == 0,
      "Lottie/Rive runtime must not step frames in the frame path — route "
      "through VectorImporter");
}

}  // namespace motion
#endif  // MOTION_VECTOR_RUNTIME_STEP_FROM_FRAME_PATH

#endif  // MOTION_VECTOR_IMPORTER_H_
