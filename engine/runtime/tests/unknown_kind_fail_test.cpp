// Batch 2 RED pin — TC-RUNTIME-051 / AC-RUNTIME-042 (FR-RUNTIME-014).
//
// Unknown component kind on the local gallery path must fail the proof gate
// (proofGate=fail): no golden re-baselined while red, no gray-box or bundled
// substitution rendered.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery(const void*, size_t, Renderer&, Swapchain&, string*)`
// entry. Mapping (see unit-tests/T3.md): proofGate=fail == returns false
// with a non-blank diagnostic; no substitution == zero DrawCmds submitted;
// re-baseline blocked == the false return the golden tooling consumes.
//
// Hermetic: C++17 + committed headers only; in-memory MUI1 buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-T4): unknown-kind rows fall through `FamilyKind` to
// `ShapedText` (gallery_present.cpp) and present as a tonal card, so
// `PresentGallery` returns true with cmds submitted — the three *_fail_*
// cases below FAIL until the coder wires the fail-closed gate.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

std::vector<std::uint8_t> MakeAsset(const std::string& json) {
  std::vector<std::uint8_t> out;
  out.reserve(12 + json.size());
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::uint32_t n = static_cast<std::uint32_t>(json.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : json) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

// Known-kind control: Button is an explicit FamilyKind member.
const char kKnownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}}"
    "]}]}";

// "QuantumFluxCapacitor" appears in no FamilyKind branch and in no spec
// vocabulary: the unknown-kind probe for AC-RUNTIME-042.
const char kUnknownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}},"
    "{\"type\":\"QuantumFluxCapacitor\",\"props\":{\"label\":\"Mystery "
    "action\"}}"
    "]}]}";

bool check_known_kind_control_passes() {
  auto asset = MakeAsset(kKnownIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (!ok) {
    std::printf("  control asset unexpectedly failed: %s\n", error.c_str());
    return false;
  }
  if (target.captured.empty()) {
    std::printf("  control asset emitted no cmds\n");
    return false;
  }
  return true;
}

bool check_unknown_kind_fails_gate() {
  auto asset = MakeAsset(kUnknownIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (ok) {
    std::printf("  unknown kind presented as pass (substitution rendered)\n");
    return false;
  }
  return true;
}

bool check_unknown_kind_emits_nothing() {
  auto asset = MakeAsset(kUnknownIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (!target.captured.empty()) {
    std::printf("  unknown kind submitted %zu substitution cmds\n",
                target.captured.size());
    return false;
  }
  return true;
}

bool check_unknown_kind_error_names_kind() {
  auto asset = MakeAsset(kUnknownIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (error.empty()) {
    std::printf("  unknown kind left no diagnostic\n");
    return false;
  }
  return true;
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): no host
  // font exists in this environment, and the default embedded test face
  // covers A/B only — fail-closed shaping rejects gallery labels, so the
  // known-kind control fails without it (environment fault, not prod
  // regression; the committed gallery_text_emission_test fails
  // identically). Unopenable in-memory bytes (committed s_spine_test.cpp /
  // harfbuzz_parity_test.cpp convention) route shaping through the
  // deterministic salted-fallback stub. Test-only, no downloads, no system
  // fonts, no device. The fail-closed gate under test runs before shaping,
  // so the red legs still fail at the gate.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_known_kind_control_passes);
  RUN_CASE(check_unknown_kind_fails_gate);
  RUN_CASE(check_unknown_kind_emits_nothing);
  RUN_CASE(check_unknown_kind_error_names_kind);
  if (failures > 0) {
    std::printf("unknown_kind_fail_test: %d failing\n", failures);
    return 1;
  }
  std::printf("unknown_kind_fail_test: all passing\n");
  return 0;
}
