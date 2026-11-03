// Batch 2 RED pin — TC-RUNTIME-055 / AC-RUNTIME-046 (FR-RUNTIME-014).
//
// While any of AC-RUNTIME-042/043/044/045 is red, the proof gate blocks
// golden re-baseline (proofGate=fail); green re-baseline requires all four
// green.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery(const void*, size_t, Renderer&, Swapchain&, string*)`
// entry. Mapping (see unit-tests/T4.md): the in-process re-baseline block
// signal is the false return with a non-blank diagnostic and zero submitted
// cmds; the golden tooling must refuse re-baseline while it observes that
// signal (tooling side owned by the coder, out of this TU's reach).
//
// TC-RUNTIME-054 (AC-RUNTIME-045, placeholder-scan clean) has no C++ TU by
// construction: a hermetic unit test may not read the real source tree, so
// the scan stays the shell check
// `! grep -rq -e PlaceholderInflate -e placeholder_subtree --
// engine/runtime/src` (currently clean; see contract).
//
// Hermetic: C++17 + committed headers only; in-memory MUI1 buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-T4): the unknown-kind red proof still presents as pass
// with substitution cmds, so `check_red_unknown_kind_blocks_rebaseline`
// FAILS until the coder wires the fail-closed gate.
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

// All-green: every row a known FamilyKind member with resolvable props.
const char kGreenIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}}"
    "]}]}";

// Red via AC-RUNTIME-042: unknown component kind.
const char kRedUnknownIr[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}},"
    "{\"type\":\"QuantumFluxCapacitor\",\"props\":{\"label\":\"Mystery "
    "action\"}}"
    "]}]}";

const char kRedUnreadable[] = "not-a-mui1-asset-at-all";

bool check_red_unknown_kind_blocks_rebaseline() {
  auto asset = MakeAsset(kRedUnknownIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  // Re-baseline block signal: fail + diagnostic + nothing emitted.
  if (ok || error.empty() || !target.captured.empty()) {
    std::printf("  red proof did not block re-baseline (ok=%d cmds=%zu)\n",
                static_cast<int>(ok), target.captured.size());
    return false;
  }
  return true;
}

bool check_red_unreadable_blocks_rebaseline() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(kRedUnreadable, sizeof(kRedUnreadable) - 1,
                                    renderer, target, &error);
  if (ok || error.empty() || !target.captured.empty()) {
    std::printf("  unreadable proof did not block re-baseline (ok=%d)\n",
                static_cast<int>(ok));
    return false;
  }
  return true;
}

bool check_green_proof_allows_present() {
  auto asset = MakeAsset(kGreenIr);
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (!ok) {
    std::printf("  green proof unexpectedly failed: %s\n", error.c_str());
    return false;
  }
  if (target.captured.empty()) {
    std::printf("  green proof emitted no cmds\n");
    return false;
  }
  return true;
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): see
  // unknown_kind_fail_test.cpp — unopenable in-memory bytes route the
  // green-proof control through the deterministic salted-fallback stub
  // (test-only, no downloads, no system fonts, no device). The red-proof
  // gates run before shaping, so the red legs still fail at the gate.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_red_unknown_kind_blocks_rebaseline);
  RUN_CASE(check_red_unreadable_blocks_rebaseline);
  RUN_CASE(check_green_proof_allows_present);
  if (failures > 0) {
    std::printf("proof_gate_blocks_rebaseline_test: %d failing\n", failures);
    return 1;
  }
  std::printf("proof_gate_blocks_rebaseline_test: all passing\n");
  return 0;
}
