// Batch 1 contract tests — FR-GRAPHICS-010 / AC-GRAPHICS-029
// (TC-GRAPHICS-029 hermetic counterpart; gallery goldens stay T9/T10).
// T1: retained vocabulary extends to 11 PrimitiveKinds with copyable
// PrimitivePayload; unknown kind fails closed (RULE-RUNTIME-001).
// T2: StateLayer (payload.stateColor/stateOpacity) renders tint rect at
// bounds; error surface unchanged.
// Hermetic: C++17 + committed runtime headers only; FakeSwapchain records
// cmds in-process, fake DeviceCaps, no GPU, no network, no env/config,
// no filesystem, no live services.
// Binds ONLY to the T1/T2 `Interface:` lines; never to implementation.
// RED STATE (expected): `runtime/renderer.h` has only 4 kinds and no
// PrimitivePayload yet (production code owned by the coder), so this file
// must fail to compile until the coder implements T1/T2.
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

// Baseline device caps: Vulkan 1.1 with no extensions required.
runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// In-process fake swapchain: records every submitted cmd for
// record-and-verify. No GPU, no surface.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override { captured.push_back(cmd); }
  std::vector<runtime::DrawCmd> captured;
};

runtime::DrawCmd make_cmd(runtime::PrimitiveKind kind, float opacity,
                          std::uint32_t nodeId) {
  runtime::DrawCmd cmd;
  cmd.kind = kind;
  cmd.bounds = {10.0f, 20.0f, 100.0f, 50.0f};
  cmd.radius = 8.0f;
  cmd.opacity = opacity;
  cmd.nodeId = nodeId;
  return cmd;
}

}  // namespace

// T1: all 11 vocabulary kinds draw — one cmd per kind is recorded in the
// swapchain with kind/nodeId intact; draw returns None.
static bool check_all_eleven_kinds_draw() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const std::vector<runtime::PrimitiveKind> kinds = {
      runtime::PrimitiveKind::Rect,
      runtime::PrimitiveKind::RoundedRect,
      runtime::PrimitiveKind::Image,
      runtime::PrimitiveKind::Text,
      runtime::PrimitiveKind::StateLayer,
      runtime::PrimitiveKind::Ripple,
      runtime::PrimitiveKind::ElevationShadow,
      runtime::PrimitiveKind::CircleArc,
      runtime::PrimitiveKind::VectorIcon,
      runtime::PrimitiveKind::ShapedText,
      runtime::PrimitiveKind::OverlayScrim,
  };
  std::vector<runtime::DrawCmd> cmds;
  std::uint32_t node = 100;
  for (const auto kind : kinds) {
    cmds.push_back(make_cmd(kind, 1.0f, node++));
  }
  if (renderer.draw(cmds, target) != runtime::DrawError::None) return false;
  if (target.captured.size() != kinds.size()) return false;
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    if (target.captured[i].kind != kinds[i]) return false;
    if (target.captured[i].nodeId != cmds[i].nodeId) return false;
  }
  return true;
}

// T1: PrimitivePayload is copyable (noexcept-copyable per plan A1) and a
// DrawCmd copy preserves kind/bounds/payload.
static bool check_payload_copyable_cmd_copy_preserves() {
  static_assert(std::is_copy_constructible<runtime::PrimitivePayload>::value,
                "PrimitivePayload must be copyable");
  static_assert(std::is_copy_assignable<runtime::PrimitivePayload>::value,
                "PrimitivePayload must be copy-assignable");
  runtime::DrawCmd cmd = make_cmd(runtime::PrimitiveKind::StateLayer, 1.0f, 61);
  cmd.payload.stateColor = 0xFF112233u;
  cmd.payload.stateOpacity = 0.5f;
  const runtime::DrawCmd copied = cmd;
  if (copied.kind != runtime::PrimitiveKind::StateLayer) return false;
  if (copied.payload.stateColor != 0xFF112233u) return false;
  if (copied.payload.stateOpacity != 0.5f) return false;
  if (copied.bounds.w != 100.0f || copied.bounds.h != 50.0f) return false;
  runtime::DrawCmd assigned;
  assigned = cmd;
  return assigned.payload.stateColor == 0xFF112233u &&
         assigned.payload.stateOpacity == 0.5f;
}

// T2: StateLayer renders tint rect at bounds — recorded cmd keeps bounds,
// stateColor, stateOpacity; draw returns None.
static bool check_statelayer_records_tint_at_bounds() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::DrawCmd cmd =
      make_cmd(runtime::PrimitiveKind::StateLayer, 1.0f, 71);
  cmd.bounds = {5.0f, 7.0f, 64.0f, 32.0f};
  cmd.payload.stateColor = 0xFF445566u;
  cmd.payload.stateOpacity = 0.25f;
  if (renderer.draw({cmd}, target) != runtime::DrawError::None) return false;
  if (target.captured.size() != 1) return false;
  const runtime::DrawCmd& recorded = target.captured[0];
  if (recorded.kind != runtime::PrimitiveKind::StateLayer) return false;
  if (recorded.nodeId != 71) return false;
  if (recorded.bounds.x != 5.0f || recorded.bounds.y != 7.0f ||
      recorded.bounds.w != 64.0f || recorded.bounds.h != 32.0f) {
    return false;
  }
  return recorded.payload.stateColor == 0xFF445566u &&
         recorded.payload.stateOpacity == 0.25f;
}

// T2 boundary: StateLayer opacity edges 0.0 (no visible tint, still
// submits None) and 1.0 round-trip exactly.
static bool check_statelayer_opacity_edges_zero_and_one() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::DrawCmd zero =
      make_cmd(runtime::PrimitiveKind::StateLayer, 1.0f, 81);
  zero.payload.stateColor = 0xFF0000FFu;
  zero.payload.stateOpacity = 0.0f;
  runtime::DrawCmd one =
      make_cmd(runtime::PrimitiveKind::StateLayer, 1.0f, 82);
  one.payload.stateColor = 0xFF0000FFu;
  one.payload.stateOpacity = 1.0f;
  if (renderer.draw({zero, one}, target) != runtime::DrawError::None) {
    return false;
  }
  if (target.captured.size() != 2) return false;
  return target.captured[0].payload.stateOpacity == 0.0f &&
         target.captured[1].payload.stateOpacity == 1.0f;
}

// Named case: each MVP primitive kind draws — one cmd per kind is recorded
// in the swapchain.
static bool check_each_primitive_kind_draws() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const std::vector<runtime::DrawCmd> cmds = {
      make_cmd(runtime::PrimitiveKind::Rect, 1.0f, 1),
      make_cmd(runtime::PrimitiveKind::RoundedRect, 1.0f, 2),
      make_cmd(runtime::PrimitiveKind::Image, 1.0f, 3),
      make_cmd(runtime::PrimitiveKind::Text, 1.0f, 4),
  };
  if (renderer.draw(cmds, target) != runtime::DrawError::None) return false;
  if (target.captured.size() != 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (target.captured[i].kind != cmds[i].kind) return false;
    if (target.captured[i].nodeId != cmds[i].nodeId) return false;
  }
  return true;
}

// Named case: per-node opacity is preserved on every recorded cmd.
static bool check_per_node_opacity_recorded() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const std::vector<runtime::DrawCmd> cmds = {
      make_cmd(runtime::PrimitiveKind::Rect, 0.25f, 11),
      make_cmd(runtime::PrimitiveKind::Text, 0.75f, 12),
      make_cmd(runtime::PrimitiveKind::Image, 0.5f, 13),
  };
  if (renderer.draw(cmds, target) != runtime::DrawError::None) return false;
  if (target.captured.size() != 3) return false;
  for (std::size_t i = 0; i < 3; ++i) {
    if (target.captured[i].opacity != cmds[i].opacity) return false;
  }
  return true;
}

// Named case: opacity edge values 0.0 (fully transparent) and 1.0 (opaque)
// round-trip exactly — no clamping, no defaulting.
static bool check_opacity_edges_zero_and_one() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const std::vector<runtime::DrawCmd> cmds = {
      make_cmd(runtime::PrimitiveKind::Rect, 0.0f, 21),
      make_cmd(runtime::PrimitiveKind::RoundedRect, 1.0f, 22),
  };
  if (renderer.draw(cmds, target) != runtime::DrawError::None) return false;
  if (target.captured.size() != 2) return false;
  return target.captured[0].opacity == 0.0f &&
         target.captured[1].opacity == 1.0f;
}

// Named case: an empty cmd list is a no-op success — nothing recorded, no
// error.
static bool check_empty_cmd_list_is_noop_success() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const std::vector<runtime::DrawCmd> cmds;
  if (renderer.draw(cmds, target) != runtime::DrawError::None) return false;
  return target.captured.empty();
}

// Named case (RULE-RUNTIME-001): an unsupported primitive kind returns
// DrawError::UnknownPrimitive instead of falling back to a default box —
// no Rect is recorded for the unknown cmd.
static bool check_unknown_primitive_returns_error_no_default_box() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::DrawCmd bad = make_cmd(static_cast<runtime::PrimitiveKind>(999),
                                  1.0f, 31);
  const std::vector<runtime::DrawCmd> cmds = {bad};
  if (renderer.draw(cmds, target) != runtime::DrawError::UnknownPrimitive) {
    return false;
  }
  for (const auto& recorded : target.captured) {
    if (recorded.nodeId == 31) return false;  // no default box for node 31.
  }
  return true;
}

// Named case: baseline gate — an unsupported baseline (Vulkan 1.0 caps)
// makes draw a no-op returning the baseline error: nothing is recorded.
static bool check_unsupported_baseline_gate_noops() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 0;
  runtime::Renderer renderer(caps);
  FakeSwapchain target;
  const std::vector<runtime::DrawCmd> cmds = {
      make_cmd(runtime::PrimitiveKind::Rect, 1.0f, 41),
  };
  if (renderer.draw(cmds, target) !=
      runtime::DrawError::UnsupportedBaseline) {
    return false;
  }
  if (!target.captured.empty()) return false;
  return !renderer.lastError().empty();
}

// Named case: a mixed list containing one unknown kind still reports
// UnknownPrimitive and records no default box for the unknown node, while
// the valid cmds are recorded with their opacity intact.
static bool check_mixed_list_reports_unknown_kind() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::DrawCmd bad = make_cmd(static_cast<runtime::PrimitiveKind>(999),
                                  0.5f, 52);
  const std::vector<runtime::DrawCmd> cmds = {
      make_cmd(runtime::PrimitiveKind::Text, 0.5f, 51),
      bad,
  };
  if (renderer.draw(cmds, target) != runtime::DrawError::UnknownPrimitive) {
    return false;
  }
  bool sawValid = false;
  for (const auto& recorded : target.captured) {
    if (recorded.nodeId == 52) return false;  // no default box.
    if (recorded.nodeId == 51) {
      if (recorded.opacity != 0.5f) return false;
      sawValid = true;
    }
  }
  return sawValid;
}

// Named case (T1 fail-closed kept): unknown kind among the new vocabulary
// still returns UnknownPrimitive and records no box for that nodeId.
static bool check_unknown_kind_among_new_vocabulary_fail_closed() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::DrawCmd ok =
      make_cmd(runtime::PrimitiveKind::StateLayer, 1.0f, 91);
  ok.payload.stateColor = 0xFF123456u;
  ok.payload.stateOpacity = 0.5f;
  runtime::DrawCmd bad = make_cmd(static_cast<runtime::PrimitiveKind>(999),
                                  1.0f, 92);
  const std::vector<runtime::DrawCmd> cmds = {ok, bad};
  if (renderer.draw(cmds, target) != runtime::DrawError::UnknownPrimitive) {
    return false;
  }
  bool sawOk = false;
  for (const auto& recorded : target.captured) {
    if (recorded.nodeId == 92) return false;
    if (recorded.nodeId == 91) sawOk = true;
  }
  return sawOk;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
       ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_all_eleven_kinds_draw);
  RUN_CASE(check_payload_copyable_cmd_copy_preserves);
  RUN_CASE(check_statelayer_records_tint_at_bounds);
  RUN_CASE(check_statelayer_opacity_edges_zero_and_one);
  RUN_CASE(check_each_primitive_kind_draws);
  RUN_CASE(check_per_node_opacity_recorded);
  RUN_CASE(check_opacity_edges_zero_and_one);
  RUN_CASE(check_empty_cmd_list_is_noop_success);
  RUN_CASE(check_unknown_primitive_returns_error_no_default_box);
  RUN_CASE(check_unsupported_baseline_gate_noops);
  RUN_CASE(check_mixed_list_reports_unknown_kind);
  RUN_CASE(check_unknown_kind_among_new_vocabulary_fail_closed);
  if (failures > 0) {
    std::printf("render_smoke_test: %d failing\n", failures);
    return 1;
  }
  std::printf("render_smoke_test: all passing\n");
  return 0;
}
