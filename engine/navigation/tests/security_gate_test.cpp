// Stage-4 security-review add-on — 6 requested security unit tests.
// Verdict context: CLEARED WITH NOTES, 0 CRITICAL/HIGH/MEDIUM; rows map to
// the "Security tests requested" table in security-review.md.
// Hermetic: C++17 + navigation headers only; in-process graphs, stub hooks,
// dummy literals; no secrets, no network/config/filesystem reads.
// Binds to snapshot.h APIs verbatim:
//   `SnapshotResult captureSnapshotWithContent(const NavGraph&,
//     ScrollProvider, FocusProvider) noexcept;`
//   `SnapshotResult truncateSnapshot(const std::string&, size_t,
//     TelemetrySink) noexcept;`
//   `RestoreResult commitRestore(const std::string&, ValidateFn,
//     GuardFn) noexcept;`
// Does NOT modify any existing test file.
#include <cstdio>
#include <stdexcept>
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

std::string detailsBlob() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  SnapshotResult c = captureSnapshot(g);
  return c.ok ? c.blob : "";
}

std::string bigBlob() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  g.apply(NavOp{NavOpType::SwitchTab, "", 1});
  g.apply(NavOp{NavOpType::Push, "settings", 1});
  std::string pad(512, 'x');
  ScrollProvider scroll = [pad](const std::string& r) -> std::string {
    return "scroll-" + r + "-" + pad;
  };
  FocusProvider focus = [pad](const std::string& r) -> std::string {
    return "focus-" + r + "-" + pad;
  };
  SnapshotResult r = captureSnapshotWithContent(g, scroll, focus);
  return r.ok ? r.blob : "";
}

// 1. Throwing scroll/focus provider -> opt-out (empty), no throw,
//    capture still ok and commit still possible.
bool check_throwing_providers_opt_out_no_throw() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  ScrollProvider badScroll = [](const std::string&) -> std::string {
    throw std::runtime_error("dummy-scroll-failure");
  };
  FocusProvider badFocus = [](const std::string&) -> std::string {
    throw std::runtime_error("dummy-focus-failure");
  };
  SnapshotResult r{false, "", SnapshotError::CORRUPT};
  try {
    r = captureSnapshotWithContent(g, badScroll, badFocus);
  } catch (...) {
    return false;  // must never throw
  }
  if (!r.ok || r.error != SnapshotError::NONE) return false;
  // Opt-out: throwing providers degrade to empty, so no content section.
  if (r.blob.find(";content=") != std::string::npos) return false;
  // Commit still possible on the degraded blob.
  ValidateFn allow = [](const std::string&) { return true; };
  GuardFn permit = [](const std::string&) { return true; };
  RestoreResult rr{false, RestoreError::CORRUPT};
  try {
    rr = commitRestore(r.blob, allow, permit);
  } catch (...) {
    return false;
  }
  return rr.committed && rr.error == RestoreError::NONE;
}

// 2. Throwing validator/guard hook -> deny (fail-closed), commits nothing,
//    never throws.
bool check_throwing_hooks_deny_no_commit() {
  std::string blob = detailsBlob();
  if (blob.empty()) return false;
  ValidateFn throwingValidate = [](const std::string&) -> bool {
    throw std::runtime_error("dummy-validator-failure");
  };
  GuardFn throwingGuard = [](const std::string&) -> bool {
    throw std::runtime_error("dummy-guard-failure");
  };
  GuardFn permit = [](const std::string&) { return true; };
  ValidateFn allow = [](const std::string&) { return true; };
  RestoreResult rv{false, RestoreError::NONE};
  RestoreResult rg{false, RestoreError::NONE};
  try {
    rv = commitRestore(blob, throwingValidate, permit);
    rg = commitRestore(blob, allow, throwingGuard);
  } catch (...) {
    return false;  // must never throw
  }
  return !rv.committed && rv.error == RestoreError::VALIDATION_REJECTED &&
         !rg.committed && rg.error == RestoreError::GUARD_DENIED;
}

// 3. Throwing telemetry sink -> swallowed, truncation still correct.
bool check_throwing_sink_swallowed_truncation_correct() {
  std::string blob = bigBlob();
  if (blob.empty()) return false;
  size_t budget = blob.size() / 2;
  TelemetrySink badSink = [](const std::string&, const std::string&) {
    throw std::runtime_error("dummy-sink-failure");
  };
  SnapshotResult r{false, "", SnapshotError::CORRUPT};
  try {
    r = truncateSnapshot(blob, budget, badSink);
  } catch (...) {
    return false;  // must never throw
  }
  return r.ok && r.truncated && r.blob.size() <= budget &&
         r.error == SnapshotError::NONE;
}

// 4. Adversarial content-injection blob: route-like payload inside
//    `;content=` (`|`, `,`, `;` smuggling) -> gate decisions unaffected
//    (validator/guard see only structural routes).
bool check_content_injection_gate_unaffected() {
  std::string structural = detailsBlob();
  if (structural.empty()) return false;
  std::string evil =
      structural +
      ";content=scroll:details|evil,focus:details;tabs=evil|settings";
  std::vector<std::string> seenValidate;
  std::vector<std::string> seenGuard;
  ValidateFn recordingValidate = [&seenValidate](const std::string& r) {
    seenValidate.push_back(r);
    if (r == "evil" || r.find("evil") != std::string::npos) return false;
    return true;
  };
  GuardFn recordingGuard = [&seenGuard](const std::string& r) {
    seenGuard.push_back(r);
    if (r == "evil" || r.find("evil") != std::string::npos) return false;
    return true;
  };
  RestoreResult r{false, RestoreError::CORRUPT};
  try {
    r = commitRestore(evil, recordingValidate, recordingGuard);
  } catch (...) {
    return false;
  }
  // Smuggled "evil" route must never reach the gate, so an evil-rejecting
  // hook still commits the structural routes.
  if (!r.committed || r.error != RestoreError::NONE) return false;
  for (const auto& id : seenValidate) {
    if (id.find("evil") != std::string::npos) return false;
    if (id.find("|") != std::string::npos ||
        id.find(";") != std::string::npos ||
        id.find(",") != std::string::npos)
      return false;
  }
  for (const auto& id : seenGuard) {
    if (id.find("evil") != std::string::npos) return false;
    if (id.find("|") != std::string::npos ||
        id.find(";") != std::string::npos ||
        id.find(",") != std::string::npos)
      return false;
  }
  return !seenValidate.empty() && !seenGuard.empty();
}

// 5. Oversized + tampered blobs -> fail-closed, no crash.
// Oversized/garbage blobs -> CORRUPT + commit nothing. Structurally tampered
// blobs (garbage `sel=`, empty stacks) -> the documented fail-closed mapping
// in snapshot.h: UNKNOWN_ROUTE_HOME (home-only commit) or CORRUPT (no
// commit); attacker content is never committed. Pinned per security-review
// "Checks Performed and Clean" (tampered-blob row, verdict CLEARED).
bool check_oversized_tampered_blobs_corrupt_no_commit() {
  ValidateFn allow = [](const std::string&) { return true; };
  GuardFn permit = [](const std::string&) { return true; };
  const std::string noCommit[] = {
      std::string(1 << 20, 'A'),  // oversized garbage
      detailsBlob() + std::string(1 << 20, 'x'),  // valid prefix + oversize
      "|||===;;;",
      "",
  };
  for (const auto& b : noCommit) {
    RestoreResult r{false, RestoreError::NONE};
    try {
      r = commitRestore(b, allow, permit);
    } catch (...) {
      return false;  // must never throw
    }
    if (r.committed || r.error != RestoreError::CORRUPT) return false;
  }
  // Structurally tampered: fail-closed disjunction only — never a commit of
  // attacker content, never a throw.
  const std::string tampered[] = {
      "NAV1;v=1;sel=99999999999999999999;tabs=home;modals=;drawer=0",
      "NAV1;v=1;sel=abc;tabs=home;modals=;drawer=0",
      "NAV1;v=1;sel=0;tabs=;modals=;drawer=0",
  };
  for (const auto& b : tampered) {
    RestoreResult r{false, RestoreError::NONE};
    try {
      r = commitRestore(b, allow, permit);
    } catch (...) {
      return false;  // must never throw
    }
    bool corruptNoCommit =
        !r.committed && r.error == RestoreError::CORRUPT;
    bool homeFallback =
        r.error == RestoreError::UNKNOWN_ROUTE_HOME;  // home-only commit
    if (!(corruptNoCommit || homeFallback)) return false;
  }
  return true;
}

// 6. Delimiter-alphabet violation payload (provider returning `|`, `;`,
//    `,focus:`) -> documented degradation pinned: degrades deterministically,
//    never crashes, never validates a smuggled route.
bool check_delimiter_violation_degrades_deterministically() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  ScrollProvider evilScroll = [](const std::string& r) -> std::string {
    return "a|b;c,focus:" + r + "|smuggled";
  };
  FocusProvider empty = [](const std::string&) -> std::string { return ""; };
  SnapshotResult a{false, "", SnapshotError::CORRUPT};
  SnapshotResult b{false, "", SnapshotError::CORRUPT};
  try {
    a = captureSnapshotWithContent(g, evilScroll, empty);
    b = captureSnapshotWithContent(g, evilScroll, empty);
  } catch (...) {
    return false;  // never crashes
  }
  if (!a.ok || !b.ok) return false;
  if (a.blob != b.blob) return false;  // deterministic degradation
  // Structural prefix stays intact: strip at `;content=`, still restores.
  std::string structural = a.blob;
  std::string::size_type pos = structural.find(";content=");
  if (pos != std::string::npos) structural.erase(pos);
  NavGraph h(kRoutes);
  if (!h.restore(structural).ok) return false;
  // The smuggled route is never validated: an evil-rejecting hook still
  // commits, and no hook ever sees the smuggled token.
  std::vector<std::string> seen;
  ValidateFn recording = [&seen](const std::string& r) {
    seen.push_back(r);
    if (r.find("smuggled") != std::string::npos) return false;
    return true;
  };
  GuardFn permit = [](const std::string&) { return true; };
  RestoreResult r{false, RestoreError::CORRUPT};
  try {
    r = commitRestore(a.blob, recording, permit);
  } catch (...) {
    return false;
  }
  if (!r.committed) return false;
  for (const auto& id : seen) {
    if (id.find("smuggled") != std::string::npos) return false;
  }
  return true;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_throwing_providers_opt_out_no_throw(),
              "throwing_providers_opt_out_no_throw_commit_possible");
  EXPECT_TRUE(check_throwing_hooks_deny_no_commit(),
              "throwing_hooks_deny_commit_nothing");
  EXPECT_TRUE(check_throwing_sink_swallowed_truncation_correct(),
              "throwing_sink_swallowed_truncation_correct");
  EXPECT_TRUE(check_content_injection_gate_unaffected(),
              "content_injection_gate_unaffected");
  EXPECT_TRUE(check_oversized_tampered_blobs_corrupt_no_commit(),
              "oversized_tampered_blobs_corrupt_no_commit");
  EXPECT_TRUE(check_delimiter_violation_degrades_deterministically(),
              "delimiter_violation_degrades_deterministically");
  if (failures > 0) {
    std::printf("security_gate_test: %d failing\n", failures);
    return 1;
  }
  std::printf("security_gate_test: all passing\n");
  return 0;
}
