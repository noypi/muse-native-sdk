// T4 contract tests — FR-TEXT-009 / AC-TEXT-020 (TC-TEXT-020).
// Test-only item: replays a fixed script (compose, commit, delete, cursor
// move, surrounding-text query) through ALooperBridge + GapBuffer and asserts
// the final state. No new production API.
// Binds ONLY to the T1/T2/T3 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/composition_roundtrip.h` does not exist yet
// (the surrounding-text query step needs the T3 seam), so this file must
// FAIL TO COMPILE. That is the recorded red state.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/alooper_bridge.h"
#include "runtime/composition_roundtrip.h"
#include "runtime/gap_buffer.h"

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

// --- T4 helpers ------------------------------------------------------------

static ImeEvent make_ev(ImeEvent::Kind kind, const char* text = "",
                        int a = 0, int b = 0) {
  ImeEvent ev;
  ev.kind = kind;
  ev.text = text;
  ev.arg_a = a;
  ev.arg_b = b;
  return ev;
}

// Fixed script per plan detail: compose "nihon", replace with the multibyte
// commit "日本ab", move cursor to end, delete 1 char, surrounding-text query.
// Reference final bytes: "日本a".
static void run_fixed_script(GapBuffer& buf, ALooperBridge& bridge) {
  bridge.post(make_ev(ImeEvent::Kind::kCompose, "nihon"));
  bridge.drain();
  ImeEvent del;
  del.kind = ImeEvent::Kind::kDelete;
  del.arg_a = 5;
  bridge.post(del);
  bridge.drain();
  bridge.post(make_ev(ImeEvent::Kind::kCommit, "日本ab"));
  bridge.drain();
  // "日本ab" is 8 bytes; move the cursor to the end.
  bridge.post(make_ev(ImeEvent::Kind::kMoveCursor, "", 8, 8));
  bridge.drain();
  ImeEvent del_one;
  del_one.kind = ImeEvent::Kind::kDelete;
  del_one.arg_a = 1;
  bridge.post(del_one);
  bridge.drain();
  bridge.post(make_ev(ImeEvent::Kind::kQuerySurrounding));
  bridge.drain();
}

// --- T4 cases (each maps to AC-TEXT-020) ------------------------------------

// Acceptance: final C++ buffer state is byte-identical to the reference.
static bool check_script_final_state_reference() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  run_fixed_script(buf, bridge);
  if (buf.to_string() != "日本a") return false;
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  return st.text == "日本a";
}

// Acceptance: the bridge touched JSI zero times across the whole script.
static bool check_script_zero_jsi() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  run_fixed_script(buf, bridge);
  if (bridge.stats().jsi_touches != 0) return false;
  return bridge.stats().events_dropped == 0;
}

// Every scripted event is delivered exactly once.
static bool check_script_all_events_delivered() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  run_fixed_script(buf, bridge);
  // compose, delete, commit, move, delete, query = 6 posts.
  return bridge.stats().events_delivered == 6;
}

// The surrounding-text query after the script matches the buffer bytes.
static bool check_script_query_matches_buffer() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  run_fixed_script(buf, bridge);
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  if (st.text != buf.to_string()) return false;
  return st.sel_lo <= st.text.size() && st.sel_hi <= st.text.size();
}

// The mid-script delete removes exactly the composed ASCII prefix.
static bool check_script_prefix_delete_applies() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  bridge.post(make_ev(ImeEvent::Kind::kCompose, "nihon"));
  bridge.drain();
  if (buf.to_string() != "nihon") return false;
  ImeEvent del;
  del.kind = ImeEvent::Kind::kDelete;
  del.arg_a = 5;
  bridge.post(del);
  bridge.drain();
  if (!buf.to_string().empty()) return false;
  return bridge.stats().jsi_touches == 0;
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
  RUN_CASE(check_script_final_state_reference);
  RUN_CASE(check_script_zero_jsi);
  RUN_CASE(check_script_all_events_delivered);
  RUN_CASE(check_script_query_matches_buffer);
  RUN_CASE(check_script_prefix_delete_applies);
  if (failures > 0) {
    std::printf("ime_session_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_session_test: all passing\n");
  return 0;
}
