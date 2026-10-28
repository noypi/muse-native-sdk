// T3 contract tests — FR-NAV-009 / AC-NAV-027 (TC-NAV-027).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Binds to the T3 `Interface:`
// line verbatim (see unit-tests/T3.md):
//   `SnapshotResult truncateSnapshot(const std::string& blob, size_t maxBytes,
//     TelemetrySink sink);`
//   `using TelemetrySink = void(const std::string& event,
//     const std::string& detail)`
// events `"nav.snapshot.truncated"` with detail naming dropped section;
// returns `{ok=true, blob<=maxBytes, truncated=true}` or `{ok=false,
// error=CORRUPT}` when even the home root exceeds budget. Priority:
// focus → scroll → deepest stack entries; home root never dropped.
// Depends on: T1, T2.
// T1 constraint respected: the bare blob has no `;content=` suffix because
// `NavGraph::restore` parses exactly 5 `;`-fields, so these cases only
// ever expect content sections to be dropped first and never expect the
// structural prefix to be cut.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/snapshot.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

const std::vector<std::string> kRoutes = {"home", "details", "settings"};

// Recording sink compatible with both `std::function<void(const
// std::string&, const std::string&)>` and plain function-pointer
// spellings of TelemetrySink: a captureless lambda converts to either.
struct Recorder {
  std::vector<std::string> events;
  std::vector<std::string> details;
  static void push(void* ctx, const std::string& e, const std::string& d) {
    auto* self = static_cast<Recorder*>(ctx);
    self->events.push_back(e);
    self->details.push_back(d);
  }
};

// Large opted-in content blob: 2-tab graph with long scroll/focus payloads.
std::string bigBlob() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  g.apply(NavOp{NavOpType::SwitchTab, "", 1});
  g.apply(NavOp{NavOpType::Push, "settings", 1});
  g.apply(NavOp{NavOpType::PushModal, "details", 0});
  std::string pad(512, 'x');
  ScrollProvider scroll = [pad](const std::string& r) -> std::string {
    return "scroll-" + r + "-" + pad;
  };
  FocusProvider focus = [pad](const std::string& r) -> std::string {
    return "focus-" + r + "-" + pad;
  };
  SnapshotResult r = captureSnapshotWithContent(g, scroll, focus);
  if (!r.ok) return "";
  return r.blob;
}

bool check_under_budget_no_truncation_no_telemetry() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, blob.size() + 1024, sink);
  return r.ok && !r.truncated && r.blob == blob &&
         r.error == SnapshotError::NONE && rec.events.empty();
}

bool check_exact_budget_no_truncation() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, blob.size(), sink);
  return r.ok && !r.truncated && r.blob.size() <= blob.size() &&
         rec.events.empty();
}

bool check_oversize_2x_truncates_within_budget() {
  std::string blob = bigBlob();
  if (blob.size() < 64) return false;
  size_t budget = blob.size() / 2;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, budget, sink);
  return r.ok && r.truncated && r.blob.size() <= budget &&
         r.error == SnapshotError::NONE && !rec.events.empty();
}

bool check_truncation_emits_truncated_event() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, blob.size() / 2, sink);
  if (!r.ok || !r.truncated || rec.events.empty()) return false;
  for (const auto& e : rec.events) {
    if (e != "nav.snapshot.truncated") return false;
  }
  return true;
}

bool check_telemetry_detail_names_dropped_section() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, blob.size() / 2, sink);
  if (!r.ok || rec.details.empty()) return false;
  // Every drop names what was dropped: focus, scroll, or a route/stack id.
  for (const auto& d : rec.details) {
    if (d.empty()) return false;
  }
  bool namesFocusOrScroll = false;
  for (const auto& d : rec.details) {
    if (d.find("focus") != std::string::npos ||
        d.find("scroll") != std::string::npos ||
        d.find("details") != std::string::npos ||
        d.find("settings") != std::string::npos)
      namesFocusOrScroll = true;
  }
  return namesFocusOrScroll;
}

bool check_focus_dropped_before_scroll() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  // Budget just under full size: only the lowest-priority (focus) section
  // should go; scroll payload must survive.
  size_t budget = blob.size() > 64 ? blob.size() - 64 : 0;
  if (budget == 0) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, budget, sink);
  if (!r.ok || !r.truncated) return false;
  bool focusGone = r.blob.find("focus-") == std::string::npos;
  bool scrollKept = r.blob.find("scroll-") != std::string::npos;
  return focusGone && scrollKept;
}

bool check_keeps_home_root() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot(blob, blob.size() / 2, sink);
  if (!r.ok) return false;
  // Structural prefix must still parse: strip content at `;content=` (T1
  // constraint) and restore; home root must survive.
  std::string structural = r.blob;
  std::string::size_type pos = structural.find(";content=");
  if (pos != std::string::npos) structural.erase(pos);
  NavGraph h(kRoutes);
  if (!h.restore(structural).ok) return false;
  return h.serialize().find("home") != std::string::npos &&
         r.blob.find("home") != std::string::npos;
}

bool check_home_only_over_budget_is_corrupt() {
  NavGraph g(kRoutes);  // fresh: home root only, smallest possible blob
  SnapshotResult c = captureSnapshot(g);
  if (!c.ok || c.blob.empty()) return false;
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  // Budget smaller than even the home-root structural blob.
  SnapshotResult r = truncateSnapshot(c.blob, 8, sink);
  return !r.ok && r.error == SnapshotError::CORRUPT;
}

bool check_empty_blob_is_corrupt() {
  Recorder rec;
  auto sink = [&rec](const std::string& e, const std::string& d) {
    Recorder::push(&rec, e, d);
  };
  SnapshotResult r = truncateSnapshot("", 1024, sink);
  return !r.ok && r.error == SnapshotError::CORRUPT;
}

bool check_truncate_never_throws() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  bool noThrow = true;
  SnapshotResult r{false, "", SnapshotError::CORRUPT};
  try {
    Recorder rec;
    auto sink = [&rec](const std::string& e, const std::string& d) {
      Recorder::push(&rec, e, d);
    };
    r = truncateSnapshot(blob, blob.size() / 2, sink);
  } catch (...) {
    noThrow = false;
  }
  return noThrow && r.ok;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_under_budget_no_truncation_no_telemetry(),
              "under_budget_no_truncation_no_telemetry");
  EXPECT_TRUE(check_exact_budget_no_truncation(),
              "exact_budget_no_truncation");
  EXPECT_TRUE(check_oversize_2x_truncates_within_budget(),
              "oversize_2x_truncates_within_budget");
  EXPECT_TRUE(check_truncation_emits_truncated_event(),
              "truncation_emits_truncated_event");
  EXPECT_TRUE(check_telemetry_detail_names_dropped_section(),
              "telemetry_detail_names_dropped_section");
  EXPECT_TRUE(check_focus_dropped_before_scroll(),
              "focus_dropped_before_scroll");
  EXPECT_TRUE(check_keeps_home_root(), "keeps_home_root");
  EXPECT_TRUE(check_home_only_over_budget_is_corrupt(),
              "home_only_over_budget_is_corrupt");
  EXPECT_TRUE(check_empty_blob_is_corrupt(), "empty_blob_is_corrupt");
  EXPECT_TRUE(check_truncate_never_throws(), "truncate_never_throws");
  if (failures > 0) {
    std::printf("snapshot_truncation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("snapshot_truncation_test: all passing\n");
  return 0;
}
