// BUG-LAYOUT-005 RED proof — TC-LAYOUT-055 / AC-LAYOUT-052 (FR-LAYOUT-016).
//
// Per-component shape roles, no generic fallback: Button (corner.full),
// Card (corner.medium), TopAppBar (corner.none -> 0) must paint distinct
// radii resolved through each component's own tabled role in
// engine/layout/m3_matrix.json (tokenTable.shape: md.comp.button.shape,
// md.comp.card.shape, md.comp.top-app-bar.shape; shape: full / medium /
// none). The runtime never consumes per-component roles today: every
// component row paints the hardcoded 20.0f card/shadow literals
// (gallery_present.cpp EmitComponentRowCmds) over the 12.0f generic
// kM3ShapeDp/kSurfaceShapeDp fallback (presenter.cpp:206,
// gallery_present.cpp:821), with 16.0f/18.0f Image/band literals on family
// rows — so painted radius stays hardcoded regardless of table role.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `PresentGallery` entry with in-memory MUI1 buffers and a FakeSwapchain.
// Hermetic: C++17 + committed headers only; no network, no env/config
// reads, no filesystem, no live services. Expected role strings are
// hardcoded below with matrix provenance (reading m3_matrix.json at test
// time would be filesystem I/O, forbidden in unit tests).
//
// Expected RED (pre-fix): all per-component radii collapse to 20.0f, so
// every *_differs_* / *_is_zero / *_not_hardcoded case below FAILS until
// the coder wires painted radius to the per-component resolver
// (getComponentTable via engine/layout/m3_registry.js).
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
//     SystemFontEnumerator::LastResortFace) — the radii under test never
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

// Matrix provenance (engine/layout/m3_matrix.json, tokenSnapshot
// 2026-10-28): Button shape md.sys.shape.corner.full, Card shape
// md.sys.shape.corner.medium, TopAppBar shape md.sys.shape.corner.none.
// Medium resolves to 12dp per the committed M3 baseline (presenter.cpp:
// "shape corner.medium -> 12dp"); none resolves to 0.0f; full (pill)
// resolves through its own role and must differ from both.
constexpr float kMediumDp = 12.0f;
constexpr float kNoneDp = 0.0f;
// Staged assumption: no committed M3 baseline source for 28dp in-repo; the
// pin below locks the shipped Button full (pill) value so future changes
// are deliberate, not silent.
constexpr float kFullDp = 28.0f;
// Producer literals pinned as the bug (never edited here, coder-owned):
// generic 12.0f fallback + hardcoded family radii.
constexpr float kGenericFallbackDp = 12.0f;
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

// One-component Showcase IR: a single component row of the named kind.
std::string single_component_ir(const std::string& comp) {
  return "{\"version\":1,\"roots\":["
         "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
         "\"props\":{},\"slots\":[],\"children\":["
         "{\"type\":\"#text\",\"value\":\"probe\"},"
         "{\"type\":\"" +
         comp +
         "\",\"props\":{\"label\":\"probe\"}}"
         "]}]}";
}

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

// Max RoundedRect radius painted for a single-component page (the card leg
// of EmitComponentRowCmds). Returns -1 when the page presents nothing.
float card_radius_for(const std::string& comp) {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::vector<std::uint8_t> asset = MakeAsset(single_component_ir(comp));
  std::string error;
  if (!runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                               &error)) {
    return -1.0f;
  }
  float best = -1.0f;
  for (const auto& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::RoundedRect && cmd.radius > best) {
      best = cmd.radius;
    }
  }
  return best;
}

bool is_hardcoded_literal(float r) {
  return nearly_equal(r, kHardcodedCardDp) ||
         nearly_equal(r, kHardcodedImageDp) ||
         nearly_equal(r, kHardcodedBandDp);
}

// Harness: single-component pages present a card cmd each.
bool check_pages_present_with_cards() {
  return card_radius_for("Button") >= 0.0f &&
         card_radius_for("Card") >= 0.0f &&
         card_radius_for("TopAppBar") >= 0.0f;
}

// AC-LAYOUT-052: full (Button) pins to 28dp (pill), not merely nonzero.
bool check_button_full_is_28() {
  const float button = card_radius_for("Button");
  return button >= 0.0f && nearly_equal(button, kFullDp);
}

// AC-LAYOUT-052: full (Button) vs medium (Card) roles paint differently.
bool check_button_full_differs_from_card_medium() {
  const float button = card_radius_for("Button");
  const float card = card_radius_for("Card");
  return button >= 0.0f && card >= 0.0f && !nearly_equal(button, card);
}

// AC-LAYOUT-052: none (TopAppBar) resolves to 0, unlike medium (Card).
bool check_topappbar_none_is_zero_unlike_card() {
  const float top = card_radius_for("TopAppBar");
  const float card = card_radius_for("Card");
  return top >= 0.0f && card >= 0.0f && nearly_equal(top, kNoneDp) &&
         !nearly_equal(card, kNoneDp);
}

// FR-LAYOUT-016: no generic fallback — Card medium is role-bound, and no
// painted radius may be one of the hardcoded producer literals.
bool check_no_hardcoded_literal_radii() {
  const float button = card_radius_for("Button");
  const float card = card_radius_for("Card");
  const float top = card_radius_for("TopAppBar");
  if (button < 0.0f || card < 0.0f || top < 0.0f) return false;
  if (is_hardcoded_literal(button) || is_hardcoded_literal(card) ||
      is_hardcoded_literal(top)) {
    return false;
  }
  // Card medium is role-bound to 12dp, not to the generic fallback path:
  // the numeral coincides with kGenericFallbackDp, so the inequality legs
  // above (not this equality) are what distinguish role from fallback.
  return nearly_equal(card, kMediumDp);
}

}  // namespace

int main() {
  int failures = 0;
  RUN_CASE(check_pages_present_with_cards);
  RUN_CASE(check_button_full_is_28);
  RUN_CASE(check_button_full_differs_from_card_medium);
  RUN_CASE(check_topappbar_none_is_zero_unlike_card);
  RUN_CASE(check_no_hardcoded_literal_radii);
  if (failures > 0) {
    std::printf("shape_token_test: %d failing (RED: radius hardcoded)\n",
                failures);
    return 1;
  }
  std::printf("shape_token_test: all passing\n");
  return 0;
}
