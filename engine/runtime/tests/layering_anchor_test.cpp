// T1 contract tests — BUG-RUNTIME-004 / AC-M3-022 (IME-inset-safe clause) /
// AC-LAYOUT-035 (no full-tree relayout) / FR-LAYOUT-012 (typed inset bus).
// Hermetic: C++17 + committed runtime headers only; no ANativeWindow, no
// GPU, no network, no env/config reads, no filesystem, no live services.
// Mirrors gallery_layering_test.cpp conventions (anonymous namespace,
// RUN_CASE, printf main; FakeSwapchain not needed — the anchor mutates the
// scene in place and emits evidence to stdout, never to a swapchain).
//
// Binds to the T1 `Interface:` line verbatim:
//   `void anchorSheetToImeInset(LayeringScene& scene, float imeBottomPx)`
// — in-place sheet-Y mutation, no relayout.
// (Port from branch run/bug-m3-fidelity-008; record source commit hash in
// the contract.)
//
// ASSUMPTION N1 (negative clamp): the plan does not pin negative inset
// handling; this suite tests clamp-to-0 (matches the run checkout's
// gallery_present.cpp and the header's "Non-finite inset resolves to 0"
// spirit). If the planner settles otherwise, update the expectation here.
// ASSUMPTION N2 (non-finite): NaN and +/-Inf resolve to sheetY == 0.0f
// with fullRelayouts untouched and no throw (per header comment).
// ASSUMPTION N3 (evidence shape): per-frame evidence emission is the
// printf line `[layering] per-frame evidence sheetY=<y> inset=<i>
// relayout=none fullRelayouts=<n>` on host builds. The stdout-capture
// probe below is POSIX-only (mirrors gallery_layering_test.cpp).
#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/gallery_present.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

runtime::LayeringScene valid_scene() {
  runtime::LayeringScene scene;
  scene.valid = true;
  return scene;
}

// T1 happy path: sheet-Y tracks the animated inset value exactly.
bool check_sheet_tracks_positive_inset_exactly() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, 120.0f);
  if (std::fabs(scene.sheetY - 120.0f) > 1e-6f) return false;
  runtime::anchorSheetToImeInset(scene, 87.5f);
  return std::fabs(scene.sheetY - 87.5f) < 1e-6f;
}

// T1 happy path: zero inset is a no-op that keeps sheetY at 0.
bool check_zero_inset_keeps_sheet_at_zero() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, 0.0f);
  return scene.sheetY == 0.0f && scene.fullRelayouts == 0u;
}

// T1 acceptance (AC-LAYOUT-035): a stepped animated-inset sequence binds
// sheetY every frame with no jump (>1px per 0.5px step fails).
bool check_sheet_tracks_stepped_inset_without_jump() {
  runtime::LayeringScene scene = valid_scene();
  float prev = scene.sheetY;
  for (float inset = 0.0f; inset <= 240.0f; inset += 0.5f) {
    runtime::anchorSheetToImeInset(scene, inset);
    if (std::fabs(scene.sheetY - inset) > 1e-3f) return false;
    if (std::fabs(scene.sheetY - prev) > 1.0f) return false;
    prev = scene.sheetY;
  }
  return true;
}

// T1 acceptance (AC-LAYOUT-035 / AC-M3-022 no-full-tree-relayout clause):
// the stepped probe never touches the full-relayout marker.
bool check_stepped_inset_fires_no_full_tree_relayout() {
  runtime::LayeringScene scene = valid_scene();
  for (float inset = 0.0f; inset <= 240.0f; inset += 5.0f) {
    runtime::anchorSheetToImeInset(scene, inset);
  }
  return scene.fullRelayouts == 0u;
}

// T1 boundary (ASSUMPTION N1): negative inset clamps to 0, never relayouts.
bool check_negative_inset_clamps_to_zero() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, -40.0f);
  if (scene.sheetY != 0.0f) return false;
  return scene.fullRelayouts == 0u;
}

// T1 boundary (ASSUMPTION N2): NaN inset never throws, never relayouts,
// resolves to 0.
bool check_nan_inset_never_throws_and_never_relayouts() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, std::nanf(""));
  if (scene.sheetY != 0.0f) return false;
  return scene.fullRelayouts == 0u;
}

// T1 boundary (ASSUMPTION N2): +Inf inset never throws, never relayouts,
// resolves to 0.
bool check_inf_inset_never_throws_and_never_relayouts() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, 1.0f / 0.0f);
  if (scene.sheetY != 0.0f) return false;
  return scene.fullRelayouts == 0u;
}

// T1 in-place semantics: the SAME scene object is mutated (no copy, no
// return channel) — sheetY changes on the caller's object.
bool check_mutation_is_in_place_on_caller_scene() {
  runtime::LayeringScene scene = valid_scene();
  runtime::LayeringScene* addr = &scene;
  runtime::anchorSheetToImeInset(*addr, 200.0f);
  return addr == &scene && scene.sheetY == 200.0f;
}

#if !defined(_WIN32)
// Capture stdout emitted during fn() into out (in-process pipe + dup2;
// hermetic: no filesystem, no env, no network).
void capture_stdout_during(void (*fn)(), std::string* out) {
  std::fflush(stdout);
  const int saved = dup(STDOUT_FILENO);
  int fds[2];
  if (pipe(fds) != 0) {
    if (saved >= 0) close(saved);
    return;
  }
  dup2(fds[1], STDOUT_FILENO);
  fn();
  std::fflush(stdout);
  dup2(saved, STDOUT_FILENO);
  close(saved);
  close(fds[1]);
  char buf[4096];
  ssize_t n = 0;
  while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
    out->append(buf, static_cast<std::size_t>(n));
  }
  close(fds[0]);
}

namespace {
struct AnchorProbe {
  static runtime::LayeringScene* scene;
  static float inset;
  static void run() {
    runtime::anchorSheetToImeInset(*scene, inset);
  }
};
runtime::LayeringScene* AnchorProbe::scene = nullptr;
float AnchorProbe::inset = 0.0f;
}  // namespace
#endif

// T1 evidence emission: each frame emits the per-frame
// sheetY/inset/relayout=none series (plan T1 detail). POSIX-only probe.
bool check_anchor_emits_per_frame_evidence_line() {
#if defined(_WIN32)
  return false;  // stdout-capture probe is POSIX-only; verified on host
#else
  runtime::LayeringScene scene = valid_scene();
  AnchorProbe::scene = &scene;
  AnchorProbe::inset = 96.0f;
  std::string captured;
  capture_stdout_during(&AnchorProbe::run, &captured);
  AnchorProbe::scene = nullptr;
  if (captured.find("sheetY=96.0") == std::string::npos) return false;
  if (captured.find("inset=96.0") == std::string::npos) return false;
  if (captured.find("relayout=none") == std::string::npos) return false;
  return true;
#endif
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_sheet_tracks_positive_inset_exactly);
  RUN_CASE(check_zero_inset_keeps_sheet_at_zero);
  RUN_CASE(check_sheet_tracks_stepped_inset_without_jump);
  RUN_CASE(check_stepped_inset_fires_no_full_tree_relayout);
  RUN_CASE(check_negative_inset_clamps_to_zero);
  RUN_CASE(check_nan_inset_never_throws_and_never_relayouts);
  RUN_CASE(check_inf_inset_never_throws_and_never_relayouts);
  RUN_CASE(check_mutation_is_in_place_on_caller_scene);
  RUN_CASE(check_anchor_emits_per_frame_evidence_line);
  if (failures > 0) {
    std::printf("layering_anchor_test: %d failing\n", failures);
    return 1;
  }
  std::printf("layering_anchor_test: all passing\n");
  return 0;
}
