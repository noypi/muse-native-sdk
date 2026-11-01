#include "motion/vector_importer.h"

namespace {

// Placeholder decodability proxy, mirroring motion/asset.h conventions: a
// payload counts as decodable when every byte is plain text (printable ASCII
// plus tab/LF/CR). No allocation, no I/O; cannot throw.
bool isDecodableByte(uint8_t b) {
  return b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E);
}

}  // namespace

motion::GraphNode VectorImporter::import(
    AssetFormat f, const std::vector<uint8_t>& bytes, ImportError* err) noexcept {
  ImportError result = ImportError::kOk;
  motion::GraphNode node;
  if (f != AssetFormat::kLottie && f != AssetFormat::kRive) {
    result = ImportError::kUnsupported;
  } else if (bytes.empty()) {
    result = ImportError::kUnsupported;
  } else {
    for (uint8_t b : bytes) {
      if (!isDecodableByte(b)) {
        result = ImportError::kDecodeFailed;
        break;
      }
    }
  }
  // Emit a value-leaf graph node reusing the graph.h shape; deterministic
  // payload-derived value keeps imports observable without stepping.
  if (result == ImportError::kOk && !bytes.empty()) {
    node.value.value =
        static_cast<float>(bytes.size() % 256) / 255.0f;
  }
  last_ = result;
  ++imports_;
  (void)imports_;
  if (err != nullptr) *err = result;
  return node;
}
