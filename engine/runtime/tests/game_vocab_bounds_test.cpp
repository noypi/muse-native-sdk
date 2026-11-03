// Batch 1 RED pin — FR-GRAPHICS-018 / AC-GRAPHICS-049 (TC-GRAPHICS-052).
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/game_vocab.h` public header only; no
// device, no network, no env/config reads, no filesystem, no live services.
// Determinism + boundedness + fail-closed over-cap per AC-049.
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/game_vocab.h` (+ sources + CMake
// registration) — `runtime/game_vocab.h: No such file or directory`.
#include <cstdint>
#include <cstdio>
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

}  // namespace

int main() {
  RUN_CASE(check_deterministic_bytes);
  RUN_CASE(check_whole_frame_or_nothing);
  RUN_CASE(check_over_cap_instances_rejected);
  RUN_CASE(check_over_deep_stack_rejected);
  RUN_CASE(check_frame_counted_only_for_appended);
  RUN_CASE(check_opacity_group_alpha_composes);
  if (failures > 0) {
    std::printf("game_vocab_bounds_test: %d failing\n", failures);
    return 1;
  }
  std::printf("game_vocab_bounds_test: all passing\n");
  return 0;
}
