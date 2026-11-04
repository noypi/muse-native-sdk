// BUG-LAYOUT-005 RED proof — TC-LAYOUT-056 / AC-LAYOUT-017 (FR-LAYOUT-008).
//
// border-radius / box-shadow / transform bind to the same resolver: the
// Card row's RoundedRect radius (border-radius leg) and its
// ElevationShadow radius+blur (box-shadow leg) must both resolve through
// the per-component shape role (Card: corner.medium -> 12dp), never the
// hardcoded literals. Today both legs paint hardcoded values —
// card.radius = 20.0f and shadow.radius = 20.0f
// (gallery_present.cpp EmitComponentRowCmds:821+ region) over the 12.0f
// generic kM3ShapeDp/kSurfaceShapeDp fallback (presenter.cpp:206,
// gallery_present.cpp:821) — so the compile leg passes while the
// resolver-bound radius and shadow legs FAIL.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery` entry with an in-memory MUI1 Card buffer and a
// FakeSwapchain. Hermetic: C++17 + committed headers only; no network, no
// env/config reads, no filesystem, no live services.
//
// Expected RED (pre-fix): card radius paints 20.0f, shadow radius paints
// 20.0f — the *_is_medium_12 / *_same_resolver / *_not_hardcoded cases
// below FAIL until the coder binds all three CSS effects to the one
// resolver (entry point: getComponentTable via
// engine/layout/m3_registry.js; literal sites: presenter.cpp:206,
// gallery_present.cpp:821+).
//
// ASSUMPTION (transform leg): DrawCmd carries no transform channel today,
// so transform-to-resolver binding is not unit-observable; the card+shadow
// shared-resolver pin below is the observable contract, and the coder wires
// transform to the same entry. Flagged in Blocked, not faked.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"
#include "muse/text/font_discovery.h"
#include "muse/text/glyph_atlas.h"
#include "muse/text/glyph_raster_orientation.h"
#include "muse/text/shaper.h"
#include "muse/text/text_types.h"

// ---- Hermetic boundary fakes (test-local, never production) ----
// The units under test run for REAL: the paint decisions in
// engine/runtime/src/gallery_present.cpp, layout (layout.cpp), tree
// (tree.cpp), RichBuffer, and row parsing. Faked at the boundary only:
//   * text shaping (shapeRunWithFallback / measureLine / resolveFallbackChain
//     / GlyphAtlas / GlyphUploadPreservesAdvanceDirection /
//     SystemFontEnumerator::LastResortFace) — the effects under test never
//     depend on glyph outlines; the fake yields deterministic nonzero run
//     handles for nonempty labels (mirroring the real cache contract:
//     empty -> 0 dangling sentinel) and positive advances;
//   * runtime::Renderer (ctor/draw/lastError) — replicates the real
//     renderer.cpp forwarding semantics for valid inputs (submit every
//     known-kind cmd; UnknownPrimitive on the zero-handle sentinels);
//     Real adapter: engine/runtime/src/renderer.cpp (needs system
//     freetype headers, unavailable to hermetic per-file g++);
//   * ::liveKindOverrideActive — host has no live text field, so the
//     override is inactive, exactly as on a host build without JNI;
//   * runtime::RasterizeShapedText — link-only seam; the retained path
//     under test never rasterizes (no Swapchain pixels inspected).
// Baseline note (ASSUMPTION): DeviceCaps{1,1} is supported per the existing
// suite, so the fake skips the checkBaseline gate real draw() performs.
namespace muse {
namespace text {

StatusOr<ShapedRun> shapeRunWithFallback(const std::uint8_t* bytes,
                                         std::size_t len,
                                         const TextStyle&,
                                         const FontRequest&) noexcept {
  ShapedRun run;
  if (bytes == nullptr || len == 0) return StatusOr<ShapedRun>(run);
  const std::size_t n = len > 256 ? 256 : len;
  for (std::size_t i = 0; i < n; ++i) {
    run.glyph_ids.push_back(0x100u + bytes[i]);
    run.advances.push_back(8.0f);
    run.offsets.push_back(GlyphOffset());
    run.cluster_map.push_back(static_cast<std::uint32_t>(i));
  }
  return StatusOr<ShapedRun>(run);
}

MeasuredLine measureLine(ShapedRunView run, float maxWidthPx) {
  MeasuredLine m;
  float w = 8.0f * static_cast<float>(run.glyph_count());
  if (w > maxWidthPx) w = maxWidthPx;
  if (w < 0.0f) w = 0.0f;
  m.width_px = w;
  m.glyph_count = run.glyph_count();
  return m;
}

StatusOr<FontHandle> resolveFallbackChain(std::uint32_t, const char*,
                                          const char*) noexcept {
  return StatusOr<FontHandle>(FontHandle(7));
}

bool GlyphUploadPreservesAdvanceDirection(const std::uint8_t*, std::size_t,
                                          std::size_t, std::size_t,
                                          float) noexcept {
  return true;
}

GlyphAtlas::GlyphAtlas() = default;
GlyphAtlas::GlyphAtlas(std::size_t, std::size_t) {}
Status GlyphAtlas::insert(FontHandle, float, std::uint32_t,
                          const TexelRecord&) {
  return Status::kOk;
}

FontHandle SystemFontEnumerator::LastResortFace(std::uint32_t) const {
  return FontHandle(7);
}

}  // namespace text
}  // namespace muse

bool liveKindOverrideActive() { return false; }

namespace runtime {

Renderer::Renderer(DeviceCaps caps) : caps_(caps) {}

DrawError Renderer::draw(const std::vector<DrawCmd>& cmds,
                         Swapchain& target) {
  DrawError result = DrawError::None;
  for (const auto& cmd : cmds) {
    switch (cmd.kind) {
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
      case PrimitiveKind::Text:
      case PrimitiveKind::StateLayer:
      case PrimitiveKind::Ripple:
      case PrimitiveKind::ElevationShadow:
      case PrimitiveKind::CircleArc:
      case PrimitiveKind::OverlayScrim:
        target.submit(cmd);
        break;
      case PrimitiveKind::VectorIcon:
        if (cmd.payload.assetId == 0) {
          lastError_ = "unresolvable asset id";
          result = DrawError::UnknownPrimitive;
          break;
        }
        target.submit(cmd);
        break;
      case PrimitiveKind::ShapedText:
        if (cmd.payload.glyphRunId == 0) {
          lastError_ = "dangling glyph run";
          result = DrawError::UnknownPrimitive;
          break;
        }
        target.submit(cmd);
        break;
      default:
        lastError_ = "unknown primitive kind";
        result = DrawError::UnknownPrimitive;
        break;
    }
  }
  if (result == DrawError::None) lastError_.clear();
  return result;
}

std::string Renderer::lastError() const { return lastError_; }

muse::text::StatusOr<CoverageMask> RasterizeShapedText(
    std::uint32_t, const Rect&, std::uint32_t) {
  return muse::text::Status::kMissingGlyph;
}

}  // namespace runtime

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

namespace {

// Card shape role corner.medium -> 12dp (committed M3 baseline).
constexpr float kCardMediumDp = 12.0f;
constexpr float kHardcodedCardDp = 20.0f;
constexpr float kHardcodedImageDp = 16.0f;
constexpr float kHardcodedBandDp = 18.0f;

bool nearly_equal(float a, float b) { return std::fabs(a - b) < 1e-6f; }

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

const char kCardIrJson[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"#text\",\"value\":\"probe\"},"
    "{\"type\":\"Card\",\"props\":{\"label\":\"probe\"}}"
    "]}]}";

std::vector<std::uint8_t> MakeAsset() {
  std::string json(kCardIrJson);
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

struct CardCmds {
  bool presented = false;
  bool has_card = false;
  bool has_shadow = false;
  float card_radius = -1.0f;
  float shadow_radius = -1.0f;
  float shadow_blur = 0.0f;
};

CardCmds present_card() {
  CardCmds out;
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::vector<std::uint8_t> asset = MakeAsset();
  std::string error;
  out.presented = runtime::PresentGallery(asset.data(), asset.size(),
                                          renderer, target, &error);
  if (!out.presented) return out;
  for (const auto& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::RoundedRect &&
        cmd.radius > out.card_radius) {
      out.has_card = true;
      out.card_radius = cmd.radius;
    }
    if (cmd.kind == runtime::PrimitiveKind::ElevationShadow) {
      out.has_shadow = true;
      out.shadow_radius = cmd.radius;
      out.shadow_blur = cmd.payload.shadowBlur;
    }
  }
  return out;
}

bool is_hardcoded_literal(float r) {
  return nearly_equal(r, kHardcodedCardDp) ||
         nearly_equal(r, kHardcodedImageDp) ||
         nearly_equal(r, kHardcodedBandDp);
}

// Compile leg: the Card page presents through the retained vocabulary.
bool check_card_page_presents_with_cmds() {
  const CardCmds cmds = present_card();
  return cmds.presented && cmds.has_card && cmds.has_shadow;
}

// border-radius leg: Card radius resolves to corner.medium (12dp).
bool check_card_radius_is_medium_12() {
  const CardCmds cmds = present_card();
  return cmds.presented && cmds.has_card &&
         nearly_equal(cmds.card_radius, kCardMediumDp);
}

// box-shadow leg: shadow binds to the SAME resolver (same radius) with a
// nonzero blur. Today shadow.radius is the hardcoded 20.0f literal.
bool check_card_shadow_binds_same_resolver() {
  const CardCmds cmds = present_card();
  return cmds.presented && cmds.has_card && cmds.has_shadow &&
         nearly_equal(cmds.shadow_radius, cmds.card_radius) &&
         nearly_equal(cmds.shadow_radius, kCardMediumDp) &&
         cmds.shadow_blur > 0.0f;
}

// No generic fallback: neither leg may carry a hardcoded producer literal.
bool check_no_hardcoded_literal_effects() {
  const CardCmds cmds = present_card();
  return cmds.presented && cmds.has_card && cmds.has_shadow &&
         !is_hardcoded_literal(cmds.card_radius) &&
         !is_hardcoded_literal(cmds.shadow_radius);
}

}  // namespace

int main() {
  int failures = 0;
  RUN_CASE(check_card_page_presents_with_cmds);
  RUN_CASE(check_card_radius_is_medium_12);
  RUN_CASE(check_card_shadow_binds_same_resolver);
  RUN_CASE(check_no_hardcoded_literal_effects);
  if (failures > 0) {
    std::printf("css_runtime_effect_test: %d failing (RED: effects "
                "hardcoded)\n",
                failures);
    return 1;
  }
  std::printf("css_runtime_effect_test: all passing\n");
  return 0;
}
