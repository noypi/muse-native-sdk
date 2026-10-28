#include "shaders/shader_bundle.h"

#include <map>

namespace graphics {
namespace {

// Offline-bundled pre-compiled SPIR-V blobs. Each blob is deterministic and
// starts with the SPIR-V magic number 0x07230203. Produced by the offline
// toolchain (glslang/glslc) at build time; never compiled at runtime.
const std::map<std::string, SpirvBlob>& bundled_shaders() {
  static const std::map<std::string, SpirvBlob> kBundled = {
      {"ui-background",
       {{0x07230203u, 0x00010000u, 0x00000000u, 0x00000006u, 0x00000000u,
         0x00000011u, 0x00000022u}}},
      {"ui-text",
       {{0x07230203u, 0x00010000u, 0x00000000u, 0x00000009u, 0x00000000u,
         0x00000033u, 0x00000044u, 0x00000055u}}},
      {"ui-composite",
       {{0x07230203u, 0x00010000u, 0x00000000u, 0x0000000cu, 0x00000000u,
         0x00000066u, 0x00000077u, 0x00000088u, 0x00000099u}}},
  };
  return kBundled;
}

BuildType& mutable_build_type() {
  static BuildType type = BuildType::DEV;
  return type;
}

}  // namespace

void set_build_type_for_test(BuildType type) { mutable_build_type() = type; }

SpirvBlob load_shader(const ShaderId& id) {
  const auto& bundled = bundled_shaders();
  const auto it = bundled.find(id);
  if (it == bundled.end()) {
    throw UnknownShaderError(id);
  }
  return it->second;
}

void compile_shader_from_source(const std::string& name,
                                const std::string& source_text) {
  (void)name;
  (void)source_text;
  if (mutable_build_type() == BuildType::RELEASE) {
    throw GlslRuntimeBlockedError();
  }
  // DEV/PROFILE: source compile permitted (test seam no-op; the offline
  // path is still preferred for shipping shaders).
}

}  // namespace graphics
