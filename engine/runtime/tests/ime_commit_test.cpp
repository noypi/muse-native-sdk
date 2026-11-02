// Batch 3 / T5 contract tests — AC-RUNTIME-009/012, FR-RUNTIME-011.
// RECONCILED 2026-11-02 (GR-08 T1.10, same pattern as the batch-2
// reconciliation): call sites bound to the landed production seam
// `runtime::ime_commit` (`engine/runtime/src/text_field.cpp`:
// `AttachEngineRow` / `SetFocusedEngineRow` / `CommitTextToEngineRow` /
// `EngineRowInputShown`, fail-closed, never throws) + the JNI
// `nativeCommitEngineRowText` entry + the MainActivity forwarder. The
// committed write path stays `runtime::commit_text`
// (`engine/runtime/include/runtime/text_field.h`).
// Pass criteria are IDENTICAL to the CONTRACT (reconciliation updates call
// sites, not expectations — GR-08 T1.9); failure messages keep their
// diagnostic value.
// Hermetic: C++17 + committed engine headers only (`runtime/text_field.h`,
// `runtime/tree.h`) + the TU under test linked in; no device, no JVM, no
// network, no env/config reads, no filesystem, no live services. The 1x1
// EditText vs engine-row binding is modeled, not executed: the hidden sink
// is a test-local string the production seam never touches.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "runtime/text_field.h"
#include "runtime/tree.h"

// Landed production seam (defined in `engine/runtime/src/text_field.cpp`;
// no committed header declares it yet, so the test forward-declares the
// exact signatures — verified against the TU 2026-11-02). Linked in by
// compiling the test together with the real TU (from run-checkout root):
// g++ -std=c++17 -Wall -Wextra -Wpedantic -I engine/runtime/include
//   engine/runtime/tests/ime_commit_test.cpp engine/runtime/src/text_field.cpp
//   -o /tmp/t && /tmp/t
namespace runtime {
namespace ime_commit {
void AttachEngineRow(NodeId row) noexcept;
void SetEngineRowText(NodeId row, const char* utf8, std::size_t len) noexcept;
void SetFocusedEngineRow(NodeId row) noexcept;
bool CommitTextToEngineRow(NodeId rowId, const char* utf8,
                           std::size_t len) noexcept;
bool EngineRowInputShown() noexcept;
std::string EngineRowText(NodeId row) noexcept;
void ResetEngineRowCommitState() noexcept;
}  // namespace ime_commit
}  // namespace runtime

namespace {

// Plan T5 `Interface:` carriers. `NodeId` is the committed
// `runtime/tree.h:11` type; no `Utf8Bytes` type exists in the tree
// (VARIANCE V1 in the T5 contract), so the test aliases it to std::string.
using NodeId = runtime::NodeId;
using Utf8Bytes = std::string;

constexpr NodeId kSearchRowId = 31;
constexpr NodeId kUnknownRowId = 9999;

// Bound view carrying the `mInputShown` flag (plan T5 observable). The real
// observable is `runtime::ime_commit::EngineRowInputShown()`; this mirror is
// refreshed from it after each commit for diagnostics.
struct BoundView {
  NodeId boundRow = runtime::kInvalidNodeId;
  bool mInputShown = false;
};

// The hidden 1x1 EditText pair content (MainActivity.java:604-620). The
// production seam never touches it — it stays empty by construction, which
// is exactly the routing assertion.
struct HiddenEditTextSink {
  std::string text;
};

// Reconciled routing: the real production seam. Unknown rows fail closed
// (returns false, no throw, nothing touched); empty text is a no-op success.
// The hidden sink is never written — the seam has no handle to it.
bool RouteCommitToEngineRow(BoundView& view, HiddenEditTextSink& hidden,
                            NodeId rowId, const Utf8Bytes& text) {
  (void)hidden;  // seam-never-touches assertion target; must stay empty
  const char* data = text.empty() ? "" : text.data();
  const bool ok = runtime::ime_commit::CommitTextToEngineRow(
      rowId, data, text.size());
  (void)ok;
  view.mInputShown = runtime::ime_commit::EngineRowInputShown();
  return ok;
}

struct Fixture {
  BoundView view;
  HiddenEditTextSink hidden;
  Fixture() {
    runtime::ime_commit::ResetEngineRowCommitState();
    runtime::ime_commit::AttachEngineRow(kSearchRowId);
    runtime::ime_commit::SetFocusedEngineRow(kSearchRowId);
    view.boundRow = kSearchRowId;
    view.mInputShown = runtime::ime_commit::EngineRowInputShown();
  }
  std::string text_of(NodeId id) const {
    return runtime::ime_commit::EngineRowText(id);
  }
};

// "hello" must appear in the engine Search row content.
bool check_commit_ascii_lands_in_engine_row() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "hello");
    const std::string got = fx.text_of(kSearchRowId);
    if (got != "hello") {
      std::printf("  detail: engine row holds %s, want hello\n", got.c_str());
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across commit (must never throw)\n");
    return false;
  }
}

// Multibyte commit "日本ab" (explicit bytes, no source-encoding reliance).
bool check_commit_multibyte_lands_in_engine_row() {
  try {
    Fixture fx;
    const Utf8Bytes text = "\xe6\x97\xa5\xe6\x9c\xac"
                            "ab";
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, text);
    const std::string got = fx.text_of(kSearchRowId);
    if (got != text) {
      std::printf("  detail: engine row holds %zu bytes, want %zu\n",
                   got.size(), text.size());
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across multibyte commit (must never throw)\n");
    return false;
  }
}

// The bound view must report mInputShown=true after the commit.
bool check_commit_sets_mInputShown_on_bound_view() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "hello");
    if (!fx.view.mInputShown) {
      std::printf("  detail: mInputShown=false on the engine-bound view\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across commit (must never throw)\n");
    return false;
  }
}

// A second commit appends at the caret after the first ("ab" then "cd").
bool check_second_commit_appends_at_caret() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "ab");
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "cd");
    const std::string got = fx.text_of(kSearchRowId);
    if (got != "abcd") {
      std::printf("  detail: engine row holds %s, want abcd\n", got.c_str());
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across second commit (must never throw)\n");
    return false;
  }
}

// The commit must never reach the hidden EditText sink; it belongs to the
// engine row. (Routing behavior, distinct from the content cases above.)
bool check_commit_never_reaches_hidden_edittext() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "hello");
    if (!fx.hidden.text.empty()) {
      std::printf("  detail: commit went to hidden EditText (%s)\n",
                   fx.hidden.text.c_str());
      return false;
    }
    if (fx.text_of(kSearchRowId) != "hello") {
      std::printf("  detail: engine row missing the committed text\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across commit (must never throw)\n");
    return false;
  }
}

// Fail-closed pin (PASS in red): empty text is a no-op and never throws.
bool check_empty_text_commit_is_noop_never_throws() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "ab");
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, "");
    if (fx.text_of(kSearchRowId) != "ab") {
      std::printf("  detail: empty commit mutated the row\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw on empty text (must never throw)\n");
    return false;
  }
}

// Fail-closed pin (PASS in red): unknown row touches nothing, never throws.
bool check_unknown_row_commit_fails_closed_never_throws() {
  try {
    Fixture fx;
    RouteCommitToEngineRow(fx.view, fx.hidden, kUnknownRowId, "hello");
    if (fx.text_of(kSearchRowId) != "") {
      std::printf("  detail: unknown-row commit mutated the Search row\n");
      return false;
    }
    if (fx.view.mInputShown) {
      std::printf("  detail: unknown-row commit set mInputShown\n");
      return false;
    }
    if (!fx.hidden.text.empty()) {
      std::printf("  detail: unknown-row commit leaked into hidden sink\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw on unknown row (must never throw)\n");
    return false;
  }
}

// Security pin (PASS in red): malformed bytes never throw across the seam.
bool check_malformed_utf8_commit_never_throws() {
  try {
    Fixture fx;
    const Utf8Bytes bad("\xff\xfe\x00\x01", 4);
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, bad);
    return true;  // fail-closed-or-stored is the coder's call; never throwing
                  // is the contract half pinned here.
  } catch (...) {
    std::printf("  detail: threw on malformed bytes (must never throw)\n");
    return false;
  }
}

// Stage 4 security-review pin (T5 bounded copy) — JNI 256KB cap boundary.
// Production guard: `nativeCommitEngineRowText` (jni_bridge.cpp) rejects
// `ulen < 0 || ulen > 262144` with -1 before the bounded std::string copy, so
// a commit of exactly 262144 bytes is accepted and 262145 is denied with the
// named tree-too-large denial. The JVM/JNI leg is EXTERNAL (no JVM here), so
// this file pins (a) the exact boundary predicate mirrored from the guard and
// (b) the real seam accepting a 262144-byte bounded copy without throwing.
// Synthetic bytes only.
// AC: T5 bounded copy. ASSUMPTION A-SR2: JNI-side enforcement stays EXTERNAL
// (device/JVM proof); the hermetic half pins the length predicate plus the
// at-bound real-seam copy.
namespace jni_cap {
constexpr long kJniCommitCapBytes = 262144;  // mirrors jni_bridge.cpp guard

enum class Verdict {
  kAcceptCopy,       // 0 <= ulen <= cap: bounded copy proceeds
  kDenyTreeTooLarge  // ulen < 0 || ulen > cap: named denial (-1), nothing copied
};

// Exact mirror of the `ulen < 0 || ulen > 262144 -> -1` guard.
inline Verdict GateCommitLength(long ulen) noexcept {
  if (ulen < 0 || ulen > kJniCommitCapBytes) {
    return Verdict::kDenyTreeTooLarge;
  }
  return Verdict::kAcceptCopy;
}
}  // namespace jni_cap

// Commit at exactly the bound is accepted; one byte over is denied.
bool check_jni_256kb_boundary_accept_at_bound_deny_one_over() {
  try {
    if (jni_cap::GateCommitLength(262144) != jni_cap::Verdict::kAcceptCopy) {
      std::printf("  detail: 262144-byte commit denied, want accept\n");
      return false;
    }
    if (jni_cap::GateCommitLength(262145) !=
        jni_cap::Verdict::kDenyTreeTooLarge) {
      std::printf("  detail: 262145-byte commit accepted, want denial\n");
      return false;
    }
    if (jni_cap::GateCommitLength(-1) !=
        jni_cap::Verdict::kDenyTreeTooLarge) {
      std::printf("  detail: negative length accepted, want denial\n");
      return false;
    }
    if (jni_cap::GateCommitLength(0) != jni_cap::Verdict::kAcceptCopy) {
      std::printf("  detail: empty commit denied, want accept(noop)\n");
      return false;
    }
    // The real seam performs the bounded copy at exactly the bound.
    Fixture fx;
    const Utf8Bytes at_bound(262144, 'a');
    RouteCommitToEngineRow(fx.view, fx.hidden, kSearchRowId, at_bound);
    if (fx.text_of(kSearchRowId) != at_bound) {
      std::printf("  detail: at-bound copy holds %zu bytes, want 262144\n",
                  fx.text_of(kSearchRowId).size());
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across 256KB boundary (must never throw)\n");
    return false;
  }
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
  RUN_CASE(check_commit_ascii_lands_in_engine_row);
  RUN_CASE(check_commit_multibyte_lands_in_engine_row);
  RUN_CASE(check_commit_sets_mInputShown_on_bound_view);
  RUN_CASE(check_second_commit_appends_at_caret);
  RUN_CASE(check_commit_never_reaches_hidden_edittext);
  RUN_CASE(check_empty_text_commit_is_noop_never_throws);
  RUN_CASE(check_unknown_row_commit_fails_closed_never_throws);
  RUN_CASE(check_malformed_utf8_commit_never_throws);
  RUN_CASE(check_jni_256kb_boundary_accept_at_bound_deny_one_over);
  if (failures > 0) {
    std::printf("ime_commit_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_commit_test: all passing\n");
  return 0;
}
