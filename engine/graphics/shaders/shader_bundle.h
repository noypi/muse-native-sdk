#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace graphics {

// Opaque shader identifier, constructible from a string literal.
using ShaderId = std::string;

// Pre-compiled SPIR-V blob. words[0] is always the SPIR-V magic number.
struct SpirvBlob {
  std::vector<uint32_t> words;
};

// Build flavour. Default is DEV; tests override via set_build_type_for_test.
enum class BuildType { DEV, PROFILE, RELEASE };

// Thrown when a GLSL source compile is attempted in a release build.
// what() is exactly "GLSL_RUNTIME_BLOCKED".
class GlslRuntimeBlockedError : public std::runtime_error {
 public:
  GlslRuntimeBlockedError() : std::runtime_error("GLSL_RUNTIME_BLOCKED") {}
};

// Thrown for unknown shader ids. Deliberately NOT a GlslRuntimeBlockedError
// so callers can distinguish "unknown id" from the release GLSL gate.
class UnknownShaderError : public std::runtime_error {
 public:
  explicit UnknownShaderError(const std::string& id)
      : std::runtime_error("unknown shader: " + id) {}
};

// Receipt echoing every warmed critical id, exactly once each.
class PrewarmReceipt {
 public:
  explicit PrewarmReceipt(std::vector<ShaderId> warmed)
      : warmed_(std::move(warmed)) {}
  const std::vector<ShaderId>& warmed_ids() const { return warmed_; }

 private:
  std::vector<ShaderId> warmed_;
};

// Test-only seam: overrides the build type used by the release gate.
void set_build_type_for_test(BuildType type);

// Loads the pre-compiled SPIR-V blob for id. Throws UnknownShaderError for
// unknown/hostile ids (never a blob, never the GLSL gate).
SpirvBlob load_shader(const ShaderId& id);

// Source-compile entry point. Throws GlslRuntimeBlockedError when invoked
// under a release build type; permitted (no-op) otherwise.
void compile_shader_from_source(const std::string& name,
                                const std::string& source_text);

// Warms every first-frame-critical pipeline. The receipt echoes each
// requested id exactly once. Throws on the first unknown id (never skips).
PrewarmReceipt prewarm(const std::vector<ShaderId>& critical);

}  // namespace graphics
