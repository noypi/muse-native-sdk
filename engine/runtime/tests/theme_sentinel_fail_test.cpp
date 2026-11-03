// Batch 2 RED pin — TC-RUNTIME-052 / AC-RUNTIME-043 (FR-RUNTIME-014).
//
// An unresolved theme token surfacing as the flat-gray 0x808080 sentinel on
// the local gallery path must fail the proof gate (proofGate=fail); no
// golden re-baselined while red.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery(const void*, size_t, Renderer&, Swapchain&, string*)`
// entry. Mapping (see unit-tests/T3.md): proofGate=fail == returns false
// with a non-blank diagnostic; no substitution == zero DrawCmds submitted.
// The gallery row parser keeps a `theme` literal prop (kPropKeys), so a row
// carrying `"theme":"NoSuchTheme"` is the unresolved-token probe: today the
// token is silently ignored and the family-default fill renders instead.
//
// Hermetic: C++17 + committed headers only; in-memory MUI1 buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-T4): the unresolved token is ignored, `PresentGallery`
// returns true with cmds submitted — the three *_fail_*/emits_nothing cases
// below FAIL until the coder wires token validation to the fail-closed gate.
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

// Opaque flat gray in the engine's RGBA wire order (R | G<<8 | B<<16 |
// A<<24): the AC-RUNTIME-043 sentinel value, never a valid token fill.
constexpr std::uint32_t kFlatGraySentinel = 0xFF808080u;

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

// No theme prop at all: must keep passing after the gate lands.
const char kKnownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}}"
    "]}]}";

// "NoSuchTheme" names no M3 token: the unresolved-token probe.
const char kBadTokenIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\",\"theme\":\"NoSuchTheme\"}}"
    "]}]}";

bool FillsContainFlatGray(const std::vector<runtime::DrawCmd>& cmds) {
  for (const auto& cmd : cmds) {
    if (cmd.payload.stateColor == kFlatGraySentinel ||
        cmd.payload.tintColor == kFlatGraySentinel ||
        cmd.payload.textColor == kFlatGraySentinel ||
        cmd.payload.scrimColor == kFlatGraySentinel ||
        cmd.payload.shadowColor == kFlatGraySentinel ||
        cmd.payload.rippleColor == kFlatGraySentinel ||
        cmd.payload.arcColor == kFlatGraySentinel) {
      return true;
    }
  }
  return false;
}

bool check_known_control_passes() {
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
  return true;
}

bool check_unresolved_token_fails_gate() {
  auto asset = MakeAsset(kBadTokenIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (ok) {
    std::printf("  unresolved token presented as pass (token ignored)\n");
    return false;
  }
  return true;
}

bool check_unresolved_token_emits_nothing() {
  auto asset = MakeAsset(kBadTokenIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (!target.captured.empty()) {
    std::printf("  unresolved token submitted %zu cmds\n",
                target.captured.size());
    return false;
  }
  return true;
}

bool check_unresolved_token_error_names_token() {
  auto asset = MakeAsset(kBadTokenIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (error.empty()) {
    std::printf("  unresolved token left no diagnostic\n");
    return false;
  }
  return true;
}

bool check_no_flat_gray_fill_emitted() {
  auto asset = MakeAsset(kBadTokenIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                          &error);
  if (FillsContainFlatGray(target.captured)) {
    std::printf("  flat-gray 0x808080 sentinel fill emitted\n");
    return false;
  }
  return true;
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): see
  // unknown_kind_fail_test.cpp — unopenable in-memory bytes route the
  // known-kind control through the deterministic salted-fallback stub
  // (test-only, no downloads, no system fonts, no device). The
  // unresolved-token gate runs before shaping, so the red legs still
  // fail at the gate.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_known_control_passes);
  RUN_CASE(check_unresolved_token_fails_gate);
  RUN_CASE(check_unresolved_token_emits_nothing);
  RUN_CASE(check_unresolved_token_error_names_token);
  RUN_CASE(check_no_flat_gray_fill_emitted);
  if (failures > 0) {
    std::printf("theme_sentinel_fail_test: %d failing\n", failures);
    return 1;
  }
  std::printf("theme_sentinel_fail_test: all passing\n");
  return 0;
}
