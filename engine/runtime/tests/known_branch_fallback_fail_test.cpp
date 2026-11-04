// Batch RED pin — TC-057 / AC-RUNTIME-048 (FR-RUNTIME-014).
//
// Known kind + unhandled PaintComponent branch/variant on the local gallery
// path must fail the proof gate (proofGate=fail): the tonal-card-with-name
// fallback never substitutes, no golden re-baselined while red.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery(const void*, size_t, Renderer&, Swapchain&, string*)`
// entry. Mapping (see unit-tests/T3.md): proofGate=fail == returns false
// with a non-blank diagnostic; no substitution == zero DrawCmds submitted;
// re-baseline blocked == the false return the golden tooling consumes.
//
// Probe: `Button` is a known kind (IsKnownGalleryComponent), but
// `variant="NoSuchVariant"` matches no PaintComponent Button branch
// (filled/tonal/outlined/text) and silently falls through to the
// `else // elevated` painter today. Same for `Card` (elevated/filled, else
// outlined). The spec requires the gate to reject the unhandled variant
// by name instead of substituting a silent paint.
//
// Hermetic: C++17 + committed headers only; in-memory MUI1 buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-coder): both bad-variant probes present as pass with
// cmds submitted — the *_fails_gate / emits_nothing / names_variant cases
// below FAIL until the coder wires variant validation to the fail-closed
// gate.
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

// Known-kind control: Button/variant=filled is an explicit PaintComponent
// branch — must keep passing after the gate lands.
const char kKnownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}}"
    "]}]}";

// Unhandled-variant probe: Button is known, but "NoSuchVariant" matches no
// PaintComponent Button branch (filled/tonal/outlined/text) — today it
// silently paints the `else // elevated` leg.
const char kBadButtonVariantIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"NoSuchVariant\",\"label\":"
    "\"Primary action\"}}"
    "]}]}";

// Second unhandled-variant probe: Card is known, but "NoSuchVariant" matches
// neither elevated nor filled — today it silently paints the outlined leg.
const char kBadCardVariantIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Card\",\"props\":{\"variant\":\"NoSuchVariant\",\"label\":"
    "\"Card content\"}}"
    "]}]}";

bool check_known_variant_control_passes() {
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

bool check_bad_button_variant_fails_gate() {
  auto asset = MakeAsset(kBadButtonVariantIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (ok) {
    std::printf("  bad Button variant presented as pass (substitution "
                "rendered)\n");
    return false;
  }
  return true;
}

bool check_bad_button_variant_emits_nothing() {
  auto asset = MakeAsset(kBadButtonVariantIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (!target.captured.empty()) {
    std::printf("  bad Button variant submitted %zu substitution cmds\n",
                target.captured.size());
    return false;
  }
  return true;
}

bool check_bad_button_variant_error_names_variant() {
  auto asset = MakeAsset(kBadButtonVariantIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (error.empty()) {
    std::printf("  bad Button variant left no diagnostic\n");
    return false;
  }
  if (error.find("NoSuchVariant") == std::string::npos) {
    std::printf("  diagnostic does not name the variant: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool check_bad_card_variant_fails_gate() {
  auto asset = MakeAsset(kBadCardVariantIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (ok) {
    std::printf("  bad Card variant presented as pass (substitution "
                "rendered)\n");
    return false;
  }
  return true;
}

bool check_bad_card_variant_emits_nothing() {
  auto asset = MakeAsset(kBadCardVariantIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (!target.captured.empty()) {
    std::printf("  bad Card variant submitted %zu substitution cmds\n",
                target.captured.size());
    return false;
  }
  return true;
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): see
  // unknown_kind_fail_test.cpp — unopenable in-memory bytes route the
  // known-variant control through the deterministic salted-fallback stub
  // (test-only, no downloads, no system fonts, no device). The variant gate
  // under test runs before shaping, so the red legs still fail at the gate.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_known_variant_control_passes);
  RUN_CASE(check_bad_button_variant_fails_gate);
  RUN_CASE(check_bad_button_variant_emits_nothing);
  RUN_CASE(check_bad_button_variant_error_names_variant);
  RUN_CASE(check_bad_card_variant_fails_gate);
  RUN_CASE(check_bad_card_variant_emits_nothing);
  if (failures > 0) {
    std::printf("known_branch_fallback_fail_test: %d failing\n", failures);
    return 1;
  }
  std::printf("known_branch_fallback_fail_test: all passing\n");
  return 0;
}
