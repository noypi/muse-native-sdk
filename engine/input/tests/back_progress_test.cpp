// T3 contract tests — FR-INPUT-006 / AC-INPUT-009 (TC-INPUT-009).
// Hermetic: C++17 + input headers only; nav sink is a stub recording
// double in-process; no network, no env/config reads, no filesystem,
// no JS/JNI hop (no jsi/jni headers included; delivery is synchronous
// in-process C++), no live services, no Android NDK.
// Binds to the T3 `Interface:` lines (plan.md verbatim):
//   `void BackGestureStream::OnEvent(BackPhase phase, float progress);`
//   where `BackPhase{Start,Progress,Cancel,Commit}`;
//   sink `INavBackSink::OnBackProgress(float progress);`
//   + `INavBackSink::OnBackSettled(bool committed);`
// Include path follows the engine/input convention:
//   #include "back_gesture.h" with engine/input/include on the path.
//
// ASSUMPTION (namespaces/shapes): neither the plan nor the spec settles the
// C++ namespace, the BackGestureStream construction, or the sink ownership.
// This test proposes: namespace `input`; `enum class BackPhase { Start,
// Progress, Cancel, Commit };`; `class INavBackSink` with virtual
// `OnBackProgress(float)` / `OnBackSettled(bool)`; `class BackGestureStream`
// constructed as `explicit BackGestureStream(INavBackSink* sink)` (non-owning
// observer pointer; a nullptr sink is a no-op, never a crash) and driven via
// `OnEvent(BackPhase, float)` in call order with no reordering or coalescing.
// Reconciliation rule: if the coder names them differently, update these call
// sites — not the expected behavior (ordered start→progress→cancel and
// start→progress→commit sequences with finger-tracked progress values).
//
// ASSUMPTION (Progress-without-Start): the plan pins no policy for a Progress
// event with no preceding Start. This test defines the stream as a dumb pipe:
// every OnEvent is forwarded to the sink in order; arbitration policy (if any)
// lives in nav, not here. A Progress before Start is still forwarded.
//
// ASSUMPTION (re-Start): a second Start after a terminal Cancel/Commit begins
// a new sequence; each terminal phase produces exactly one OnBackSettled.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "back_gesture.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

// Stub nav sink: records every call in order, synchronously in-process
// (proves the C++-only path — no JS/JNI hop — because the observation is
// visible immediately when OnEvent returns).
class StubNavSink : public input::INavBackSink {
 public:
  void OnBackProgress(float progress) override {
    progressCalls.push_back(progress);
  }
  void OnBackSettled(bool committed) override {
    settledCalls.push_back(committed);
  }
  std::vector<float> progressCalls;
  std::vector<bool> settledCalls;
};

bool same_progress(const std::vector<float>& got, const float* want,
                   std::size_t n) {
  if (got.size() != n) return false;
  for (std::size_t i = 0; i < n; ++i) {
    if (got[i] != want[i]) return false;
  }
  return true;
}

// TC-INPUT-009 (cancel): full start→progress→cancel sequence arrives at the
// stub sink in order with finger-tracked progress values, then settles
// uncommitted exactly once.
void case_cancel_sequence_in_order() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.25f);
  stream.OnEvent(input::BackPhase::Progress, 0.5f);
  stream.OnEvent(input::BackPhase::Cancel, 0.5f);
  const float want[] = {0.0f, 0.25f, 0.5f, 0.5f};
  check(same_progress(sink.progressCalls, want, 4) &&
            sink.settledCalls.size() == 1 && !sink.settledCalls[0],
        "cancel_sequence_in_order");
}

// TC-INPUT-009 (commit): full start→progress→commit sequence arrives in order
// and settles committed exactly once.
void case_commit_sequence_in_order() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.75f);
  stream.OnEvent(input::BackPhase::Commit, 1.0f);
  const float want[] = {0.0f, 0.75f, 1.0f};
  check(same_progress(sink.progressCalls, want, 3) &&
            sink.settledCalls.size() == 1 && sink.settledCalls[0],
        "commit_sequence_in_order");
}

// Progress values track the finger bit-exact: the sink observes exactly what
// the stream was given, with no quantization, coalescing, or reordering.
void case_progress_values_track_finger_exactly() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  const float fingers[] = {0.125f, 0.375f, 0.625f, 0.875f};
  for (float p : fingers) stream.OnEvent(input::BackPhase::Progress, p);
  const float want[] = {0.0f, 0.125f, 0.375f, 0.625f, 0.875f};
  check(same_progress(sink.progressCalls, want, 5) &&
            sink.settledCalls.empty(),
        "progress_values_track_finger_exactly");
}

// Cancel and commit are terminal and distinct: cancel settles false, commit
// settles true; nothing settles twice for one sequence.
void case_terminals_settle_exactly_once() {
  StubNavSink cancelSink;
  input::BackGestureStream cancelStream(&cancelSink);
  cancelStream.OnEvent(input::BackPhase::Start, 0.0f);
  cancelStream.OnEvent(input::BackPhase::Cancel, 0.0f);
  StubNavSink commitSink;
  input::BackGestureStream commitStream(&commitSink);
  commitStream.OnEvent(input::BackPhase::Start, 0.0f);
  commitStream.OnEvent(input::BackPhase::Commit, 1.0f);
  check(cancelSink.settledCalls.size() == 1 && !cancelSink.settledCalls[0] &&
            commitSink.settledCalls.size() == 1 && commitSink.settledCalls[0],
        "terminals_settle_exactly_once");
}

// A second Start after a terminal opens a new sequence: each terminal settles
// once, in order (false then true).
void case_restart_after_terminal_opens_new_sequence() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.3f);
  stream.OnEvent(input::BackPhase::Cancel, 0.3f);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.9f);
  stream.OnEvent(input::BackPhase::Commit, 1.0f);
  check(sink.settledCalls.size() == 2 && !sink.settledCalls[0] &&
            sink.settledCalls[1],
        "restart_after_terminal_opens_new_sequence");
}

// Boundary: Progress with no preceding Start is still forwarded in order
// (dumb-pipe stream; policy lives in nav). See ASSUMPTION above.
void case_progress_without_start_still_forwarded() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Progress, 0.42f);
  const float want[] = {0.42f};
  check(same_progress(sink.progressCalls, want, 1) &&
            sink.settledCalls.empty(),
        "progress_without_start_still_forwarded");
}

// Boundary: empty sequence (no events) produces no sink calls at all.
void case_no_events_no_sink_calls() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  (void)stream;
  check(sink.progressCalls.empty() && sink.settledCalls.empty(),
        "no_events_no_sink_calls");
}

// Robustness: a nullptr sink never crashes; events are dropped silently.
void case_null_sink_is_noop() {
  input::BackGestureStream stream(nullptr);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.5f);
  stream.OnEvent(input::BackPhase::Commit, 1.0f);
  check(true, "null_sink_is_noop");
}

// T5 contract tests — FR-INPUT-006 / AC-INPUT-011 (unit proxy for
// TC-INPUT-011 device-lab/predictive-back, external reference only).
// Binds to the T5 `Interface:` line (plan.md verbatim):
//   `bool BackGestureStream::PollSettled(int32_t frameId, float* outResidual);`
// Frame-stamp plumbing inside `engine/input/src/back_gesture.cpp` is
// CODER-OWNED — this file only extends cases against the published
// interface; if the plumbing is missing the cases below fail to compile
// (expected RED, not a malformed test).
//
// ASSUMPTION (PollSettled semantics): the plan settles only the signature,
// not the return/residual contract. This test defines:
//   - returns false while no terminal (Cancel/Commit) has been delivered;
//   - returns true once a terminal has been delivered and the settled state
//     is visible at `frameId`;
//   - on true, writes `*outResidual` with the residual transform, which must
//     be `0.0f` exactly after cancel AND after commit;
//   - a null `outResidual` is tolerated (returns the bool, writes nothing).
// ASSUMPTION (frame model): the stream stamps emission against a stubbed
// VSYNC frame id starting at 0; the settled state is visible at the emission
// frame itself and at emission+1 (i.e. within one frame id of emission).
// These tests therefore poll frame 0 (emission frame) and frame 1 and require
// both to report settled. If the coder wires an explicit frame feed (e.g. an
// OnEvent overload or SetFrame), update these call sites — not the expected
// behavior (visible within one frame; exact-zero residual).
// ASSUMPTION (no-events poll): with no terminal delivered, PollSettled
// returns false (residual untouched when outResidual is null; when non-null
// the value is not asserted — only the false return is).
void case_poll_false_before_terminal() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.5f);
  float residual = -1.0f;
  check(stream.PollSettled(0, &residual) == false,
        "poll_false_before_terminal");
}

// Cancel leaves exactly zero residual, already visible at the emission
// frame (frame 0) — the one-frame unit proxy for TC-INPUT-011.
void case_cancel_zero_residual_same_frame() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.5f);
  stream.OnEvent(input::BackPhase::Cancel, 0.5f);
  float residual = -1.0f;
  check(stream.PollSettled(0, &residual) == true && residual == 0.0f,
        "cancel_zero_residual_same_frame");
}

// Commit leaves exactly zero residual, already visible at the emission
// frame (frame 0).
void case_commit_zero_residual_same_frame() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.75f);
  stream.OnEvent(input::BackPhase::Commit, 1.0f);
  float residual = -1.0f;
  check(stream.PollSettled(0, &residual) == true && residual == 0.0f,
        "commit_zero_residual_same_frame");
}

// Settled state stays visible within one frame id of emission: polling at
// emission frame (0) and at frame 1 both report settled with exact-zero
// residual after cancel.
void case_settled_visible_within_one_frame() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Progress, 0.4f);
  stream.OnEvent(input::BackPhase::Cancel, 0.4f);
  float r0 = -1.0f, r1 = -1.0f;
  const bool s0 = stream.PollSettled(0, &r0);
  const bool s1 = stream.PollSettled(1, &r1);
  check(s0 && s1 && r0 == 0.0f && r1 == 0.0f,
        "settled_visible_within_one_frame");
}

// Boundary: no events at all — poll reports not-settled.
void case_poll_no_events_false() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  (void)stream;
  check(stream.PollSettled(0, nullptr) == false, "poll_no_events_false");
}

// Boundary/error: null outResidual never crashes; the settled bool is
// still reported after a terminal.
void case_poll_null_residual_safe() {
  StubNavSink sink;
  input::BackGestureStream stream(&sink);
  stream.OnEvent(input::BackPhase::Start, 0.0f);
  stream.OnEvent(input::BackPhase::Commit, 1.0f);
  check(stream.PollSettled(0, nullptr) == true, "poll_null_residual_safe");
}

}  // namespace

int main() {
  case_cancel_sequence_in_order();
  case_commit_sequence_in_order();
  case_progress_values_track_finger_exactly();
  case_terminals_settle_exactly_once();
  case_restart_after_terminal_opens_new_sequence();
  case_progress_without_start_still_forwarded();
  case_no_events_no_sink_calls();
  case_null_sink_is_noop();
  case_poll_false_before_terminal();
  case_cancel_zero_residual_same_frame();
  case_commit_zero_residual_same_frame();
  case_settled_visible_within_one_frame();
  case_poll_no_events_false();
  case_poll_null_residual_safe();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
