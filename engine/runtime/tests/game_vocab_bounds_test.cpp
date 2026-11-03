// Batch 1 RED pin — FR-GRAPHICS-018 / AC-GRAPHICS-049 (TC-GRAPHICS-052).
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/game_vocab.h` public header only; no
// device, no network, no env/config reads, no filesystem, no live services.
// Determinism + boundedness + fail-closed over-cap per AC-049.
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/game_vocab.h` (+ sources + CMake
// registration) — `runtime/game_vocab.h: No such file or directory`.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "runtime/game_vocab.h"

namespace {

int failures = 0;

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                    \
  } while (0)

// Identical tree + frame yields identical command bytes.
bool check_deterministic_bytes() {
  using namespace game_vocab;
  StagedFrame a = MakeValidFrame(7);
  StagedFrame b = MakeValidFrame(7);  // same tree + frame number
  auto ra = commit(a);
  auto rb = commit(b);
  if (!ra.ok || !rb.ok) return false;
  return ra.bytes == rb.bytes;
}

// Staging commits whole frames or nothing: a frame with one bad command
// commits nothing (no partial bytes for the valid prefix).
bool check_whole_frame_or_nothing() {
  using namespace game_vocab;
  StagedFrame f = MakeValidFrame(3);
  AppendInvalidCommand(f);  // one over-cap instance appended at staging
  auto r = commit(f);
  if (r.ok) return false;  // must not commit a poisoned frame
  return r.bytes.empty() && r.status == Status::kOverCap;
}

// Over-cap instance counts fail closed, never silently truncated.
bool check_over_cap_instances_rejected() {
  using namespace game_vocab;
  StagedFrame f = MakeFrameWithInstances(kMaxBatchInstances + 1);
  auto r = commit(f);
  if (r.ok) return false;
  if (r.status != Status::kOverCap) return false;
  if (r.log.empty()) return false;  // violation must log
  return r.bytes.empty();           // never partial/truncated bytes
}

// Transform/clip stack deeper than 8 fails closed.
bool check_over_deep_stack_rejected() {
  using namespace game_vocab;
  StagedFrame f = MakeFrameWithStackDepth(kMaxStackDepth + 1);
  auto r = commit(f);
  if (r.ok) return false;
  return r.status == Status::kOverCap && r.bytes.empty() && !r.log.empty();
}

// The frame counter advances only for appended (committed) commands.
bool check_frame_counted_only_for_appended() {
  using namespace game_vocab;
  uint64_t before = CommittedFrameCount();
  StagedFrame bad = MakeFrameWithStackDepth(kMaxStackDepth + 1);
  auto r = commit(bad);
  if (r.ok) return false;
  if (CommittedFrameCount() != before) return false;  // rejected: uncounted
  StagedFrame good = MakeValidFrame(4);
  auto ok = commit(good);
  if (!ok.ok) return false;
  return CommittedFrameCount() == before + 1;
}

// Per-command opacity composes with group alpha (multiplicative).
bool check_opacity_group_alpha_composes() {
  using namespace game_vocab;
  float got = EffectiveAlpha(0.5f /* command */, 0.5f /* group */);
  // 0.5 * 0.5 = 0.25 within float tolerance.
  return got > 0.249f && got < 0.251f;
}

// Review MINOR 1: non-finite/out-of-range rotation fails closed + counted.
bool check_nonfinite_rotation_rejected() {
  using namespace game_vocab;
  const float bad[] = {
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
  };
  for (int i = 0; i < 3; ++i) {
    GameCmd cmd = MakeValidCommand(GameKind::kSprite);
    cmd.rotationRad = bad[i];
    std::uint64_t before = ViolationCount();
    StageResult s = stage(cmd);
    if (s.status != Status::kOverCap || s.log.empty()) return false;
    if (ViolationCount() != before + 1) return false;  // denied + counted
    StagedFrame f = MakeValidFrame(11);
    f.commands.push_back(cmd);
    before = ViolationCount();
    auto c = commit(f);
    if (c.ok || c.status != Status::kOverCap) return false;
    if (!c.bytes.empty() || c.log.empty()) return false;  // whole-frame
    if (ViolationCount() != before + 1) return false;
  }
  return true;
}

// Review MINOR 1: out-of-atlas and NaN UVs fail closed + counted.
bool check_out_of_atlas_uv_rejected() {
  using namespace game_vocab;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  // Each row: {u0, v0, u1, v1} — one violation per row.
  const float cases[][4] = {
      {0.0f, 0.0f, 1.5f, 1.0f},   // u1 outside atlas
      {-0.1f, 0.0f, 1.0f, 1.0f},  // u0 negative
      {0.0f, 0.0f, 1.0f, 1.2f},   // v1 outside atlas
      {0.8f, 0.0f, 0.2f, 1.0f},   // u0 > u1 (unordered)
      {0.0f, 0.9f, 1.0f, 0.1f},   // v0 > v1 (unordered)
      {nan, 0.0f, 1.0f, 1.0f},    // NaN poisons comparison
  };
  for (const auto& uv : cases) {
    GameCmd cmd = MakeValidCommand(GameKind::kSprite);
    cmd.u0 = uv[0];
    cmd.v0 = uv[1];
    cmd.u1 = uv[2];
    cmd.v1 = uv[3];
    std::uint64_t before = ViolationCount();
    StageResult s = stage(cmd);
    if (s.status != Status::kOverCap || s.log.empty()) return false;
    if (ViolationCount() != before + 1) return false;
    StagedFrame f = MakeValidFrame(12);
    f.commands.push_back(cmd);
    before = ViolationCount();
    auto c = commit(f);
    if (c.ok || c.status != Status::kOverCap) return false;
    if (!c.bytes.empty() || c.log.empty()) return false;
    if (ViolationCount() != before + 1) return false;
  }
  return true;
}

// Review MINOR 1: alpha outside [0,1] (incl. NaN) fails closed + counted.
bool check_alpha_outside_range_rejected() {
  using namespace game_vocab;
  const float bad[] = {
      -0.25f,
      1.5f,
      std::numeric_limits<float>::quiet_NaN(),
  };
  for (float a : bad) {
    GameCmd cmd = MakeValidCommand(GameKind::kSprite);
    cmd.alpha = a;
    std::uint64_t before = ViolationCount();
    StageResult s = stage(cmd);
    if (s.status != Status::kOverCap || s.log.empty()) return false;
    if (ViolationCount() != before + 1) return false;
    StagedFrame f = MakeValidFrame(13);
    f.commands.push_back(cmd);
    before = ViolationCount();
    auto c = commit(f);
    if (c.ok || c.status != Status::kOverCap) return false;
    if (!c.bytes.empty() || c.log.empty()) return false;
    if (ViolationCount() != before + 1) return false;
  }
  // Boundary alpha 0 and 1 still stage.
  for (float a : {0.0f, 1.0f}) {
    GameCmd cmd = MakeValidCommand(GameKind::kSprite);
    cmd.alpha = a;
    if (stage(cmd).status != Status::kOk) return false;
  }
  return true;
}

// Security hardening: over-kMaxFrameCommands input fails closed + counted,
// while exactly kMaxFrameCommands still commits (boundary pin).
bool check_over_frame_command_cap_rejected() {
  using namespace game_vocab;
  StagedFrame f;
  f.frame = 9;
  f.stackDepth = 0;
  for (std::size_t i = 0; i < kMaxFrameCommands + 1; ++i) {
    f.commands.push_back(MakeValidCommand(GameKind::kSprite));
  }
  std::uint64_t vb = ViolationCount();
  std::uint64_t cb = CommittedFrameCount();
  auto r = commit(f);
  if (r.ok || r.status != Status::kOverCap) return false;
  if (!r.bytes.empty() || r.log.empty()) return false;  // never reserved
  if (ViolationCount() != vb + 1) return false;          // denied + counted
  if (CommittedFrameCount() != cb) return false;        // rejected: uncounted
  StagedFrame g;
  g.frame = 10;
  g.stackDepth = 0;
  for (std::size_t i = 0; i < kMaxFrameCommands; ++i) {
    g.commands.push_back(MakeValidCommand(GameKind::kSprite));
  }
  auto ok = commit(g);
  return ok.ok && ok.status == Status::kOk && !ok.bytes.empty();
}

}  // namespace

int main() {
  RUN_CASE(check_deterministic_bytes);
  RUN_CASE(check_whole_frame_or_nothing);
  RUN_CASE(check_over_cap_instances_rejected);
  RUN_CASE(check_over_deep_stack_rejected);
  RUN_CASE(check_frame_counted_only_for_appended);
  RUN_CASE(check_opacity_group_alpha_composes);
  RUN_CASE(check_nonfinite_rotation_rejected);
  RUN_CASE(check_out_of_atlas_uv_rejected);
  RUN_CASE(check_alpha_outside_range_rejected);
  RUN_CASE(check_over_frame_command_cap_rejected);
  if (failures > 0) {
    std::printf("game_vocab_bounds_test: %d failing\n", failures);
    return 1;
  }
  std::printf("game_vocab_bounds_test: all passing\n");
  return 0;
}
