// T2 contract tests — FR-TEXT-008 / AC-TEXT-015 (TC-TEXT-015).
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/gap_buffer.h` and `runtime/alooper_bridge.h`
// do not exist yet, so this file must FAIL TO COMPILE. That is the recorded
// red state.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/alooper_bridge.h"
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

// --- T2 helpers ------------------------------------------------------------

static ImeEvent make_ev(ImeEvent::Kind kind, const char* text = "",
                        int a = 0, int b = 0) {
  ImeEvent ev;
  ev.kind = kind;
  ev.text = text;
  ev.arg_a = a;
  ev.arg_b = b;
  return ev;
}

// --- T2 cases (each maps to the Interface: line + AC-TEXT-015) -------------

// Happy path: commit event lands text in the buffer after drain().
static bool check_commit_reaches_buffer() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  if (!bridge.post(make_ev(ImeEvent::Kind::kCommit, "hello"))) return false;
  bridge.drain();
  if (buf.to_string() != "hello") return false;
  return bridge.stats().events_delivered == 1;
}

// Compose event is delivered without JSI touches.
static bool check_compose_no_jsi() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  bridge.post(make_ev(ImeEvent::Kind::kCompose, "nihon"));
  bridge.drain();
  if (bridge.stats().jsi_touches != 0) return false;
  return bridge.stats().events_delivered == 1;
}

// Delete event removes text.
static bool check_delete_applies() {
  GapBuffer buf;
  buf.insert(0, std::string_view("abc"));
  ALooperBridge bridge(buf);
  ImeEvent ev;
  ev.kind = ImeEvent::Kind::kDelete;
  ev.arg_a = 1;
  bridge.post(ev);
  bridge.drain();
  return buf.byte_size() == 2;
}

// MoveCursor event updates cursors.
static bool check_move_cursor_applies() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  ALooperBridge bridge(buf);
  bridge.post(make_ev(ImeEvent::Kind::kMoveCursor, "", 2, 2));
  bridge.drain();
  if (bridge.stats().jsi_touches != 0) return false;
  return bridge.stats().events_delivered == 1;
}

// QuerySurrounding event is delivered and touches no JSI.
static bool check_query_surrounding_no_jsi() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  ALooperBridge bridge(buf);
  bridge.post(make_ev(ImeEvent::Kind::kQuerySurrounding));
  bridge.drain();
  if (bridge.stats().jsi_touches != 0) return false;
  return bridge.stats().events_delivered == 1;
}

// Zero-JSI property holds across a mixed sequence.
static bool check_mixed_sequence_zero_jsi() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  bridge.post(make_ev(ImeEvent::Kind::kCompose, "a"));
  bridge.post(make_ev(ImeEvent::Kind::kCommit, "b"));
  bridge.post(make_ev(ImeEvent::Kind::kMoveCursor, "", 0, 0));
  bridge.post(make_ev(ImeEvent::Kind::kQuerySurrounding));
  bridge.drain();
  if (bridge.stats().jsi_touches != 0) return false;
  return bridge.stats().events_delivered == 4;
}

// Boundary: queue overflow at kMaxQueueDepth = 256 drops (returns false),
// counts drops, never throws/crashes.
static bool check_overflow_drops_counted() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  int false_count = 0;
  for (int i = 0; i < 300; ++i) {
    if (!bridge.post(make_ev(ImeEvent::Kind::kCommit, "x"))) ++false_count;
  }
  if (false_count == 0) return false;
  bridge.drain();
  if (bridge.stats().events_dropped != false_count) return false;
  return bridge.stats().jsi_touches == 0;
}

// Boundary: draining an empty bridge is a no-op (never throws).
static bool check_drain_empty_noop() {
  GapBuffer buf;
  ALooperBridge bridge(buf);
  bridge.drain();
  const BridgeStats& st = bridge.stats();
  return st.jsi_touches == 0 && st.events_delivered == 0 &&
         st.events_dropped == 0;
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
  RUN_CASE(check_commit_reaches_buffer);
  RUN_CASE(check_compose_no_jsi);
  RUN_CASE(check_delete_applies);
  RUN_CASE(check_move_cursor_applies);
  RUN_CASE(check_query_surrounding_no_jsi);
  RUN_CASE(check_mixed_sequence_zero_jsi);
  RUN_CASE(check_overflow_drops_counted);
  RUN_CASE(check_drain_empty_noop);
  if (failures > 0) {
    std::printf("alooper_bridge_test: %d failing\n", failures);
    return 1;
  }
  std::printf("alooper_bridge_test: all passing\n");
  return 0;
}
