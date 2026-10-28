// T4 contract tests — FR-INPUT-009 / AC-INPUT-017 (TC-INPUT-017).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no live Android (no View.OnDragListener,
// no JNI env — Encode/DecodeClipData is a pure MIME+bytes marshal per plan
// A5; cross-app drop is asserted on MIME/payload/order, not on a device).
// Binds to the T4 `Interface:` line (reproduced verbatim in T4.md):
//   `struct DragPayload { std::string mimeType; std::vector<uint8_t> data; };
//    struct ClipData { std::string mimeType; std::vector<uint8_t> bytes; };
//    ClipData EncodeClipData(const DragPayload& p);
//    DragPayload DecodeClipData(const ClipData& c);
//    class DragSession { public: bool Start(const DragPayload& p);
//    void OnEnter(int targetId); void OnHover(int targetId, float x, float y);
//    void OnLeave(int targetId); bool Drop(int targetId); void Cancel(); };`
//   — denial: unsupported/empty MIME or `Drop` without `Start` → returns
//   `false`, session unchanged, no target callback fires.
// Include path follows the engine/runtime convention:
//   #include "input/drag_drop.h" with engine/input/include on the path.
//
// ASSUMPTION A1: hermetic ctest suite, printf PASS/FAIL, nonzero on failure,
// same shape as engine/input/tests/event_model_test.cpp.
// ASSUMPTION A5: ClipData bridge is a pure encode/decode + JNI-boundary
// struct, not live Android; reorder handoff documented, not implemented
// (LIST-ITEMANIM boundary — Drop notifies the settle path only).
// ASSUMPTION T4-OBSERVER: the plan Interface line names the session methods
// but no target-callback accessor; these tests observe callback order through
// the explicitly assumed test-only hook
//   `enum class DragTargetEvent { Enter, Hover, Leave, Drop };`
//   `void input::SetDragTargetObserverForTesting(
//      void (*)(DragTargetEvent, int targetId));`
// (nullptr clears the observer). The hook names are NOT in the plan Interface
// line — the coder provides them (or reconciles a rename via the planner;
// GR-08 T1.10) without changing the specified behavior.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "input/drag_drop.h"

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

struct ObservedCall {
  input::DragTargetEvent event;
  int targetId;
};

std::vector<ObservedCall> g_calls;

void record_call(input::DragTargetEvent e, int targetId) {
  g_calls.push_back(ObservedCall{e, targetId});
}

void reset_calls() { g_calls.clear(); }

void install_observer() {
  reset_calls();
  input::SetDragTargetObserverForTesting(&record_call);
}

void clear_observer() { input::SetDragTargetObserverForTesting(nullptr); }

input::DragPayload make_payload(const std::string& mime,
                                const std::vector<std::uint8_t>& bytes) {
  input::DragPayload p;
  p.mimeType = mime;
  p.data = bytes;
  return p;
}

// AC-INPUT-017 C++→Android direction: MIME+payload round-trips losslessly.
void case_encode_decode_round_trip_cpp_to_android() {
  const std::vector<std::uint8_t> bytes{0x00, 0x01, 0x7f, 0x80, 0xff, 0x42};
  input::DragPayload p = make_payload("text/plain", bytes);
  input::ClipData c = input::EncodeClipData(p);
  input::DragPayload back = input::DecodeClipData(c);
  check(back.mimeType == "text/plain" && back.data == bytes,
        "encode_decode_round_trip_cpp_to_android");
}

// AC-INPUT-017 Android→C++ direction: ClipData decodes then re-encodes
// losslessly.
void case_decode_encode_round_trip_android_to_cpp() {
  input::ClipData c;
  c.mimeType = "text/uri-list";
  c.bytes = std::vector<std::uint8_t>{0xde, 0xad, 0x00, 0xbe, 0xef};
  input::DragPayload p = input::DecodeClipData(c);
  input::ClipData again = input::EncodeClipData(p);
  check(p.mimeType == "text/uri-list" &&
            p.data == std::vector<std::uint8_t>({0xde, 0xad, 0x00, 0xbe, 0xef}) &&
            again.mimeType == c.mimeType && again.bytes == c.bytes,
        "decode_encode_round_trip_android_to_cpp");
}

// Boundary: empty data payload round-trips (MIME preserved, zero bytes).
void case_empty_data_round_trip() {
  input::DragPayload p = make_payload("text/plain", {});
  input::ClipData c = input::EncodeClipData(p);
  input::DragPayload back = input::DecodeClipData(c);
  check(back.mimeType == "text/plain" && back.data.empty(),
        "empty_data_round_trip");
}

// AC-INPUT-017: target log for a session reads [enter, hover+, drop].
void case_callback_order_enter_hover_drop() {
  install_observer();
  input::DragSession session;
  bool started =
      session.Start(make_payload("text/plain", {0x01, 0x02}));
  session.OnEnter(5);
  session.OnHover(5, 10.0f, 20.0f);
  session.OnHover(5, 11.0f, 21.0f);
  bool dropped = session.Drop(5);
  bool ok = started && dropped && g_calls.size() == 4 &&
            g_calls[0].event == input::DragTargetEvent::Enter &&
            g_calls[0].targetId == 5 &&
            g_calls[1].event == input::DragTargetEvent::Hover &&
            g_calls[2].event == input::DragTargetEvent::Hover &&
            g_calls[3].event == input::DragTargetEvent::Drop &&
            g_calls[3].targetId == 5;
  check(ok, "callback_order_enter_hover_drop");
  clear_observer();
}

// Leave is observed: enter → leave produces no drop.
void case_enter_leave_no_drop() {
  install_observer();
  input::DragSession session;
  session.Start(make_payload("text/plain", {0x09}));
  session.OnEnter(5);
  session.OnLeave(5);
  bool dropped = session.Drop(5);
  bool ok = !dropped && g_calls.size() == 2 &&
            g_calls[0].event == input::DragTargetEvent::Enter &&
            g_calls[1].event == input::DragTargetEvent::Leave;
  check(ok, "enter_leave_no_drop_after_leave");
  clear_observer();
}

// Denial: Drop without Start → false, session unchanged, no callback fires.
void case_drop_without_start_denied() {
  install_observer();
  input::DragSession session;
  bool dropped = session.Drop(5);
  check(!dropped && g_calls.empty(), "drop_without_start_denied");
  clear_observer();
}

// Denial: empty MIME → Start returns false, no callback fires.
void case_empty_mime_start_denied() {
  install_observer();
  input::DragSession session;
  bool started = session.Start(make_payload("", {0x01}));
  bool dropped = session.Drop(5);
  check(!started && !dropped && g_calls.empty(), "empty_mime_denied");
  clear_observer();
}

// Denial (input validation): unsupported MIME → Start returns false,
// session unchanged, no callback fires.
void case_unsupported_mime_start_denied() {
  install_observer();
  input::DragSession session;
  bool started =
      session.Start(make_payload("application/x-unsupported-blob", {0x01}));
  bool dropped = session.Drop(5);
  check(!started && !dropped && g_calls.empty(), "unsupported_mime_denied");
  clear_observer();
}

// Cancel ends the session: subsequent Drop fails and emits no drop callback.
void case_cancel_prevents_drop() {
  install_observer();
  input::DragSession session;
  session.Start(make_payload("text/plain", {0x07}));
  session.OnEnter(5);
  session.Cancel();
  bool dropped = session.Drop(5);
  bool saw_drop = false;
  for (const ObservedCall& c : g_calls) {
    if (c.event == input::DragTargetEvent::Drop) saw_drop = true;
  }
  check(!dropped && !saw_drop, "cancel_prevents_drop");
  clear_observer();
}

// Boundary: session calls never throw, even on denial paths.
void case_never_throws() {
  bool threw = false;
  try {
    install_observer();
    input::DragSession session;
    (void)session.Drop(1);
    (void)session.Start(make_payload("", {}));
    session.OnEnter(1);
    session.OnHover(1, 0.0f, 0.0f);
    session.OnLeave(1);
    session.Cancel();
    (void)session.Drop(1);
    clear_observer();
  } catch (...) {
    threw = true;
  }
  check(!threw, "never_throws");
}

}  // namespace

int main() {
  case_encode_decode_round_trip_cpp_to_android();
  case_decode_encode_round_trip_android_to_cpp();
  case_empty_data_round_trip();
  case_callback_order_enter_hover_drop();
  case_enter_leave_no_drop();
  case_drop_without_start_denied();
  case_empty_mime_start_denied();
  case_unsupported_mime_start_denied();
  case_cancel_prevents_drop();
  case_never_throws();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
