// Batch RED pin — TC-058 / AC-RUNTIME-049 (FR-RUNTIME-014).
//
// A missing/malformed numeric descriptor prop on the local gallery path must
// fail the proof gate OR emit a named `GFX-DESCRIPTOR-FALLBACK:<key>`
// diagnostic consumed by the gate; the silent PropDouble default never
// stands in blind.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery(const void*, size_t, Renderer&, Swapchain&, string*)`
// entry. Mapping (see unit-tests/T3.md): proofGate=fail == returns false
// with a non-blank diagnostic; the named-fallback alternative == success
// with `GFX-DESCRIPTOR-FALLBACK:<key>` present in the diagnostic so the
// gate can consume it; silent-blind == success with no such token.
//
// Probe: `Slider` reads its position through
// `PropDouble(desc, "value", 0.5)` (gallery_present.cpp). `"value"` is a
// kept literal prop key, so `value="not-a-number"` reaches the descriptor
// and `std::stod` throws back to the silent 0.5 today; a Slider with no
// `value` prop at all takes the same silent 0.5. Neither path fails the
// gate nor names the fallback key.
//
// Hermetic: C++17 + committed headers only; in-memory MUI1 buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-coder): both probes present as pass with no named
// diagnostic — the *_fails_or_names_* cases below FAIL until the coder
// wires numeric-prop validation (fail-closed or named-fallback) to the
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

constexpr const char* kFallbackToken = "GFX-DESCRIPTOR-FALLBACK:value";

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

// Well-formed control: Slider value=0.7 parses cleanly — must keep passing
// after the gate lands (either plain pass or pass with no fallback token).
const char kKnownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Slider\",\"props\":{\"value\":\"0.7\",\"label\":\"Volume\"}}"
    "]}]}";

// Malformed probe: value="not-a-number" throws inside stod and silently
// resolves to the 0.5 default today.
const char kMalformedIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Slider\",\"props\":{\"value\":\"not-a-number\",\"label\":"
    "\"Volume\"}}"
    "]}]}";

// Missing probe: no value prop at all — PropValue returns "" and silently
// resolves to the 0.5 default today.
const char kMissingIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Slider\",\"props\":{\"label\":\"Volume\"}}"
    "]}]}";

// Passes iff the gate failed closed (returns false, zero cmds, non-blank
// diagnostic) OR the named fallback was emitted for the gate to consume
// (diagnostic carries GFX-DESCRIPTOR-FALLBACK:value). Silent-blind pass
// (true with no token) fails.
bool check_probe_gated(const char* ir, const char* what) {
  auto asset = MakeAsset(ir);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (!ok) {
    if (error.empty()) {
      std::printf("  %s failed gate with no diagnostic\n", what);
      return false;
    }
    if (!target.captured.empty()) {
      std::printf("  %s failed gate yet submitted %zu cmds\n", what,
                  target.captured.size());
      return false;
    }
    return true;
  }
  if (error.find(kFallbackToken) != std::string::npos) return true;
  std::printf("  %s stood in blind: pass with no %s diagnostic\n", what,
              kFallbackToken);
  return false;
}

bool check_wellformed_control_passes() {
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

bool check_malformed_fails_or_names_fallback() {
  return check_probe_gated(kMalformedIr, "malformed value");
}

bool check_malformed_names_key_on_named_path() {
  // On the named-fallback path the diagnostic must name the exact key so
  // the gate can consume it; on the fail-closed path there is nothing to
  // check beyond check_malformed_fails_or_names_fallback. This case passes
  // unless the run was a silent-blind pass (no token, no fail).
  auto asset = MakeAsset(kMalformedIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (!ok) return true;  // fail-closed path: key check is vacuous.
  if (error.find(kFallbackToken) != std::string::npos) return true;
  std::printf("  malformed value passed without naming the key\n");
  return false;
}

bool check_missing_fails_or_names_fallback() {
  return check_probe_gated(kMissingIr, "missing value");
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): see
  // unknown_kind_fail_test.cpp — unopenable in-memory bytes route the
  // well-formed control through the deterministic salted-fallback stub
  // (test-only, no downloads, no system fonts, no device). The numeric-prop
  // gate under test runs before shaping, so the red legs still fail at the
  // gate.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_wellformed_control_passes);
  RUN_CASE(check_malformed_fails_or_names_fallback);
  RUN_CASE(check_malformed_names_key_on_named_path);
  RUN_CASE(check_missing_fails_or_names_fallback);
  if (failures > 0) {
    std::printf("descriptor_number_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("descriptor_number_fallback_test: all passing\n");
  return 0;
}
