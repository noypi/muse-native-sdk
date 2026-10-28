// T7 contract tests — FR-TEXT-008 / AC-TEXT-017 (TC-TEXT-017).
// Test-only item: drives a simulated 120 Hz inset animation while typing into
// a multi-line GapBuffer via ALooperBridge and asserts no dropped events and
// bounded per-frame work. No new production API.
// Binds ONLY to the T1/T2/T6 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/inset_animation.h` (T6 seam) does not exist
// yet, so this file must FAIL TO COMPILE. That is the recorded red state.
//
// ASSUMPTION (frame budget): "bounded per-frame work" is stated as
// `kFrameEventBudget = 32` posted events per frame: each per-frame `drain()`
// must deliver at most the events posted since the previous drain, and the
// script never posts more than the budget in one frame. The plan leaves the
// budget unstated, so the test states it.
// ASSUMPTION (namespace): `InsetFrame`/`InsetSync` are referenced unqualified,
// matching the T6 test. If the coder nests them, call sites reconcile at
// verify — expectations do not change.
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/alooper_bridge.h"
#include "runtime/gap_buffer.h"
#include "runtime/inset_animation.h"

// --- harness cases (match existing test style) -----------------------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T7 helpers ------------------------------------------------------------

// Stated per-frame op budget (see ASSUMPTION above).
static const int kFrameEventBudget = 32;

static ImeEvent make_commit(const char* text) {
  ImeEvent ev;
  ev.kind = ImeEvent::Kind::kCommit;
  ev.text = text;
  return ev;
}

// Simulated 120 Hz keyboard-show animation: 10 frames ramping 0 -> 270 px
// while typing 4 lines. Posts at most kFrameEventBudget events per frame,
// drains once per frame, and tags the inset sample to the frame id.
struct SessionResult {
  int frames = 0;
  int posted = 0;
  int max_drained_per_frame = 0;
};

static SessionResult run_keyboard_session(GapBuffer& buf,
                                          ALooperBridge& bridge,
                                          InsetSync& sync) {
  SessionResult r;
  const char* lines[] = {"hello world\n", "second line\n", "third line\n",
                         "fourth line\n"};
  const int kFrames = 10;
  for (int frame = 1; frame <= kFrames; ++frame) {
    // Type one line per frame for the first 4 frames (well under budget).
    if (frame <= 4) {
      bridge.post(make_commit(lines[frame - 1]));
      ++r.posted;
    }
    // One inset sample per frame, tagged to the frame deadline.
    InsetFrame f;
    f.ime_height = static_cast<float>(frame * 27);
    f.frame_id = frame;
    sync.on_inset(f);
    const int before = bridge.stats().events_delivered;
    bridge.drain();
    const int drained = bridge.stats().events_delivered - before;
    if (drained > r.max_drained_per_frame) r.max_drained_per_frame = drained;
    ++r.frames;
  }
  return r;
}

// --- T7 cases (each maps to AC-TEXT-017) ------------------------------------

// Acceptance: zero dropped input events across the scripted session.
static bool check_session_no_dropped_events() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  const SessionResult r = run_keyboard_session(buf, bridge, sync);
  if (r.posted != 4) return false;
  if (bridge.stats().events_dropped != 0) return false;
  return bridge.stats().events_delivered == r.posted;
}

// Acceptance: the multi-line buffer holds exactly the typed reference bytes.
static bool check_session_buffer_reference() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  run_keyboard_session(buf, bridge, sync);
  return buf.to_string() ==
         "hello world\nsecond line\nthird line\nfourth line\n";
}

// Acceptance: per-frame work stays within the stated op budget.
static bool check_session_per_frame_budget() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  const SessionResult r = run_keyboard_session(buf, bridge, sync);
  if (r.frames != 10) return false;
  // Each drain() handles O(events posted that frame), never the whole
  // backlog: at most one line-commit per frame here.
  if (r.max_drained_per_frame > kFrameEventBudget) return false;
  return r.max_drained_per_frame <= 1;
}

// Acceptance: inset samples stay synced to the frame deadline (lag <= 1)
// while typing proceeds.
static bool check_session_inset_stays_synced() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  run_keyboard_session(buf, bridge, sync);
  if (sync.last_applied_frame() != 10) return false;
  if (sync.height_for_frame(10) != 270.0f) return false;
  // A query for a future frame returns the latest known height.
  return sync.height_for_frame(11) == 270.0f;
}

// Acceptance: the bridge touched JSI zero times across the whole session.
static bool check_session_zero_jsi() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  run_keyboard_session(buf, bridge, sync);
  return bridge.stats().jsi_touches == 0;
}

// Error path: a 300-event burst in one frame overflows the fixed 256-deep
// queue without crashing; drops are counted, the rest is delivered.
static bool check_burst_overflow_drops_counted() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  int posted_ok = 0;
  for (int i = 0; i < 300; ++i) {
    if (bridge.post(make_commit("x"))) ++posted_ok;
  }
  bridge.drain();
  if (bridge.stats().events_dropped != 300 - posted_ok) return false;
  if (posted_ok != ALooperBridge::kMaxQueueDepth) return false;
  if (bridge.stats().events_delivered != posted_ok) return false;
  if (buf.byte_size() != static_cast<size_t>(posted_ok)) return false;
  return bridge.stats().jsi_touches == 0;
}

// Error path: negative inset samples during typing clamp to 0 and never
// disturb the buffer.
static bool check_negative_inset_during_typing() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  InsetSync sync;
  bridge.post(make_commit("abc"));
  bridge.drain();
  InsetFrame f;
  f.ime_height = -50.0f;
  f.frame_id = 1;
  try {
    sync.on_inset(f);
  } catch (...) {
    return false;
  }
  if (sync.height_for_frame(1) != 0.0f) return false;
  return buf.to_string() == "abc";
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
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_session_no_dropped_events);
  RUN_CASE(check_session_buffer_reference);
  RUN_CASE(check_session_per_frame_budget);
  RUN_CASE(check_session_inset_stays_synced);
  RUN_CASE(check_session_zero_jsi);
  RUN_CASE(check_burst_overflow_drops_counted);
  RUN_CASE(check_negative_inset_during_typing);
  if (failures > 0) {
    std::printf("keyboard_animation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("keyboard_animation_test: all passing\n");
  return 0;
}
