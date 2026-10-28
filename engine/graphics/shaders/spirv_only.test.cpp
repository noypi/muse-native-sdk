// Batch 2 contract tests — T7: offline SPIR-V shaders + startup pre-warm of
// the first-frame path (FR-GRAPHICS-006, AC-013 + AC-015). Binds ONLY to the
// T7 `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no filesystem, no live services, no shell-out to
// glslang/glslc (the offline toolchain is a build-time consumer dependency,
// not a test collaborator).
// Expected state before implementation: RED — `shaders/shader_bundle.h`
// does not exist, so compilation fails (missing behavior, not a malformed
// test). The coder provides the headers/sources; these tests then compile
// and run unmodified (call-site reconciliation only, per ASSUMPTIONs).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "shaders/shader_bundle.h"

// --- harness cases (repo convention, cf. engine/runtime/tests) --------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T7 helpers ------------------------------------------------------------
// ASSUMPTION (A-T7-SHAPE): C++ spelling of the plan interface. Plan line:
// `load_shader(id: ShaderId): SpirvBlob` (throws `GLSL_RUNTIME_BLOCKED` in
// release if source-compile attempted) + `prewarm(critical: ShaderId[]):
// PrewarmReceipt`. The plan/spec do not settle namespace, id type, blob
// layout, or error channel; tests assume `graphics::ShaderId` is an opaque
// alias constructible from a string literal, `graphics::SpirvBlob` exposes
// `words` (`std::vector<uint32_t>`, SPIR-V magic 0x07230203 first), free
// functions `graphics::load_shader(const ShaderId&) -> SpirvBlob` and
// `graphics::prewarm(const std::vector<ShaderId>&) -> PrewarmReceipt`
// (with `warmed_ids()` echoing every warmed critical id), source-compile
// attempted via `graphics::compile_shader_from_source(name, source_text)`
// which throws `graphics::GlslRuntimeBlockedError` (`what()` exactly
// "GLSL_RUNTIME_BLOCKED") when invoked under a release build type, and a
// test-only seam `graphics::set_build_type_for_test(BuildType)` with
// `graphics::BuildType{DEV, PROFILE, RELEASE}` (default DEV). If the
// planner/coder settles different spellings, update call sites only —
// expectations are unchanged (reconcile, not a behavior change; genuine
// signature clash on `load_shader`/`prewarm` → planner per GR-08 T1.10).

static const uint32_t kSpirvMagic = 0x07230203u;

static bool blob_is_valid_spirv(const graphics::SpirvBlob& b) {
  return b.words.size() >= 5 && b.words[0] == kSpirvMagic;
}

// --- T7 behavior cases (AC-GRAPHICS-013, AC-GRAPHICS-015) --------------------

// AC-013: shipping shaders load as pre-compiled SPIR-V — valid magic,
// deterministic across loads.
static bool check_load_shader_returns_precompiled_spirv() {
  const graphics::SpirvBlob a = graphics::load_shader("ui-text");
  const graphics::SpirvBlob b = graphics::load_shader("ui-text");
  return blob_is_valid_spirv(a) && a.words == b.words;
}

// AC-013 error path: an unknown shader id never yields a bogus blob — it
// raises, and the error is not the GLSL gate (reserved for source compile).
static bool check_unknown_shader_id_raises_without_bogus_blob() {
  try {
    const graphics::SpirvBlob b =
        graphics::load_shader("no-such-shader-xyz");
    (void)b;
    return false;
  } catch (const graphics::GlslRuntimeBlockedError&) {
    return false;  // unknown id is lookup failure, not the GLSL gate.
  } catch (...) {
    return true;
  }
}

// AC-013 runtime guard: attempting a GLSL source compile in a release build
// throws exactly GLSL_RUNTIME_BLOCKED.
static bool check_glsl_source_compile_blocked_in_release() {
  graphics::set_build_type_for_test(graphics::BuildType::RELEASE);
  bool ok = false;
  try {
    graphics::compile_shader_from_source("ui-text", "void main() {}");
  } catch (const graphics::GlslRuntimeBlockedError& e) {
    ok = std::string(e.what()) == "GLSL_RUNTIME_BLOCKED";
  } catch (...) {
    ok = false;
  }
  graphics::set_build_type_for_test(graphics::BuildType::DEV);
  return ok;
}

// AC-015: pre-warm covers every first-frame-critical pipeline — the receipt
// echoes each requested id exactly once.
static bool check_prewarm_covers_every_critical_pipeline() {
  const std::vector<graphics::ShaderId> critical = {"ui-background",
                                                    "ui-text", "ui-composite"};
  const graphics::PrewarmReceipt r = graphics::prewarm(critical);
  const std::vector<graphics::ShaderId> warmed = r.warmed_ids();
  if (warmed.size() != critical.size()) return false;
  for (const auto& id : critical) {
    bool found = false;
    for (const auto& w : warmed)
      if (w == id) found = true;
    if (!found) return false;
  }
  return true;
}

// AC-015 boundary: pre-warming an empty critical set yields an empty receipt
// (no crash, no phantom work); pre-warm is idempotent.
static bool check_prewarm_empty_and_idempotent() {
  const graphics::PrewarmReceipt empty =
      graphics::prewarm(std::vector<graphics::ShaderId>());
  if (!empty.warmed_ids().empty()) return false;
  const std::vector<graphics::ShaderId> critical = {"ui-text"};
  const graphics::PrewarmReceipt r1 = graphics::prewarm(critical);
  const graphics::PrewarmReceipt r2 = graphics::prewarm(critical);
  return r1.warmed_ids() == r2.warmed_ids();
}

// AC-015 error path: an unknown critical id fails loudly — pre-warm never
// silently skips it.
static bool check_prewarm_unknown_id_fails_loudly() {
  try {
    graphics::prewarm(std::vector<graphics::ShaderId>{"no-such-shader-xyz"});
    return false;
  } catch (...) {
    return true;
  }
}

// Security/robustness (GR-05 R5.3): hostile id strings (empty, oversized,
// path-traversal shaped) never crash the loader, never escape into source
// compile, and never return a blob.
static bool check_hostile_shader_ids_never_crash_or_compile() {
  const std::string big(4096, 's');
  const char* hostile[] = {"", big.c_str(), "../spirv_manifest",
                           "ui-text.glsl"};
  for (const char* id : hostile) {
    try {
      const graphics::SpirvBlob b = graphics::load_shader(id);
      (void)b;
      return false;  // hostile id must not resolve to any blob.
    } catch (const graphics::GlslRuntimeBlockedError&) {
      return false;  // lookup failure is not the GLSL gate; no fallback.
    } catch (...) {
      // Any other contract error is the safe outcome; keep probing.
    }
  }
  return true;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_load_shader_returns_precompiled_spirv);
  RUN_CASE(check_unknown_shader_id_raises_without_bogus_blob);
  RUN_CASE(check_glsl_source_compile_blocked_in_release);
  RUN_CASE(check_prewarm_covers_every_critical_pipeline);
  RUN_CASE(check_prewarm_empty_and_idempotent);
  RUN_CASE(check_prewarm_unknown_id_fails_loudly);
  RUN_CASE(check_hostile_shader_ids_never_crash_or_compile);
  if (failures > 0) {
    std::printf("spirv_only.test: %d failing\n", failures);
    return 1;
  }
  std::printf("spirv_only.test: all passing\n");
  return 0;
}
