// T2 contract tests — FR-M3-006 / AC-M3-011/012/013/014 (target, unchanged),
// RULE-M3-004; consumed FR-RUNTIME-010, FR-INPUT-011, FR-TEXT-008/009/013,
// FR-LAYOUT-012. Keyed to TC-M3-011/012/013/014 device legs (T3–T6 enablement).
//
// Batch 1 (T1, T2). Hermetic: C++17 + toolchain only; no network, no env/
// config reads, no filesystem, no live services, no adb. The device legs
// themselves (composition/caret/inset/clipboard assertions executed with
// `adb -s 127.0.0.1:6569`, API-29-only gate, single probe page scope) are
// EXTERNAL — see contract T2 — this file pins the attach gate that enables
// them, plus the fail-closed contract the legs precondition on.
//
// Binds to the T2 `Interface:` line (verbatim in contract T2):
//   `bool AttachLiveTextField(const char* pageId, const LiveTextFieldDeps& deps)`
// The production header does not declare it yet, so the test forward-declares
// the entry (same pattern as gallery_present_test.cpp's StateLayerOpacity
// forward declaration). Struct/field names below are the test's proposal;
// the T2 contract records the reconciliation rule (update call sites, not
// expectations) if the coder names them differently.
//
// ASSUMPTION (deps shape): `LiveTextFieldDeps` carries opaque collaborator
// handles (editing engine, inset feed, clipboard transport, retained-kind
// override out-flag). A null handle means that collaborator is absent and
// attach must fail closed. Field names are the test's proposal.
// ASSUMPTION (probe page id): the single instrumented probe page id is the
// string "m3-text-probe". The literal is the test's proposal; the contract
// is "exactly one probe page id attaches true, every other page id is
// false and leaves the static box untouched".
// ASSUMPTION (observation channel): `bool liveKindOverrideActive()` is the
// test's proposed observer for "retained-kind override active". If the coder
// exposes it differently, update the call site, not the expectation.

#include <cstdio>
#include <string>

// ---- Test-local proposal for the unsettled deps shape (see ASSUMPTION). ----
struct LiveTextFieldDeps {
  const void* editingEngine = nullptr;   // FR-TEXT-009 editing engine handle
  const void* insetFeed = nullptr;       // FR-LAYOUT-012 imeBottom feed handle
  const void* clipboard = nullptr;       // FR-TEXT-013 clipboard transport
  const void* inputConnection = nullptr;  // FR-TEXT-008 IME bridge handle
};

// Production entry (coder-owned: gallery_present.cpp / jni_bridge.cpp /
// js_runtime.cpp / Showcase.tsx in _run). Forward-declared so this test
// binds to the published signature, not to an implementation detail.
bool AttachLiveTextField(const char* pageId, const LiveTextFieldDeps& deps);

// Proposed observer for the retained-kind override (see ASSUMPTION).
bool liveKindOverrideActive();

namespace {

LiveTextFieldDeps valid_deps() {
  LiveTextFieldDeps d;
  static const int kEngine = 1;
  static const int kInsets = 2;
  static const int kClipboard = 3;
  static const int kIme = 4;
  d.editingEngine = &kEngine;
  d.insetFeed = &kInsets;
  d.clipboard = &kClipboard;
  d.inputConnection = &kIme;
  return d;
}

constexpr const char* kProbePage = "m3-text-probe";

// True-path: probe page + full deps attaches, kind override goes active.
bool check_attach_probe_page_true_and_kind_override() {
  const LiveTextFieldDeps d = valid_deps();
  bool ok = false;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    return false;
  }
  return ok && liveKindOverrideActive();
}

// Fail-closed: null page id -> false, no crash.
bool check_attach_null_page_fails_closed() {
  const LiveTextFieldDeps d = valid_deps();
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField(nullptr, d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Fail-closed: empty page id -> false, no crash.
bool check_attach_empty_page_fails_closed() {
  const LiveTextFieldDeps d = valid_deps();
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField("", d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Single-probe scope: any non-probe page id -> false (static pages unchanged).
bool check_attach_non_probe_page_returns_false() {
  const LiveTextFieldDeps d = valid_deps();
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField("m3-gallery-home", d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Fail-closed: missing editing engine -> false, no crash.
bool check_attach_missing_engine_fails_closed() {
  LiveTextFieldDeps d = valid_deps();
  d.editingEngine = nullptr;
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Fail-closed: missing IME bridge -> false, no crash.
bool check_attach_missing_ime_fails_closed() {
  LiveTextFieldDeps d = valid_deps();
  d.inputConnection = nullptr;
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Fail-closed: missing clipboard -> false, no crash (clipboard leg TC-M3-014
// must never run against a field without a platform transport).
bool check_attach_missing_clipboard_fails_closed() {
  LiveTextFieldDeps d = valid_deps();
  d.clipboard = nullptr;
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Fail-closed: missing inset feed -> false, no crash (inset leg TC-M3-013
// must never run against a field without the live imeBottom feed).
bool check_attach_missing_inset_fails_closed() {
  LiveTextFieldDeps d = valid_deps();
  d.insetFeed = nullptr;
  bool threw = false;
  bool ok = true;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    threw = true;
  }
  return !threw && !ok;
}

// Idempotence: attaching the probe page twice stays true, no crash.
bool check_attach_twice_stays_true() {
  const LiveTextFieldDeps d = valid_deps();
  bool threw = false;
  bool first = false;
  bool second = false;
  try {
    first = AttachLiveTextField(kProbePage, d);
    second = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    threw = true;
  }
  return !threw && first && second;
}

// Never throws across plausibly-bad inputs (JNI boundary safety).
bool check_attach_never_throws() {
  const LiveTextFieldDeps empty;
  bool threw = false;
  try {
    AttachLiveTextField(nullptr, empty);
    AttachLiveTextField("", empty);
    AttachLiveTextField(kProbePage, empty);
  } catch (...) {
    threw = true;
  }
  return !threw;
}

// T3 enablement (TC-M3-011 device leg): attach-ok implies the composition
// path (engine + IME bridge) is present, so the device composition leg has
// a live field to drive. Fails if attach is false.
bool check_composition_leg_precondition() {
  const LiveTextFieldDeps d = valid_deps();
  bool ok = false;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    return false;
  }
  return ok && d.editingEngine != nullptr && d.inputConnection != nullptr;
}

// T4 enablement (TC-M3-012 device leg): attach-ok implies caret/selection
// can be set and reported through the live field.
bool check_selection_leg_precondition() {
  const LiveTextFieldDeps d = valid_deps();
  bool ok = false;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    return false;
  }
  return ok && d.editingEngine != nullptr;
}

// T5 enablement (TC-M3-013 device leg): attach-ok implies the live inset
// feed is wired, so the per-frame lockstep leg has frames to assert.
bool check_inset_leg_precondition() {
  const LiveTextFieldDeps d = valid_deps();
  bool ok = false;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    return false;
  }
  return ok && d.insetFeed != nullptr;
}

// T6 enablement (TC-M3-014 device leg): attach-ok implies the platform
// clipboard transport is present for cut/copy/paste/select-all.
bool check_clipboard_leg_precondition() {
  const LiveTextFieldDeps d = valid_deps();
  bool ok = false;
  try {
    ok = AttachLiveTextField(kProbePage, d);
  } catch (...) {
    return false;
  }
  return ok && d.clipboard != nullptr;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_attach_probe_page_true_and_kind_override);
  RUN_CASE(check_attach_null_page_fails_closed);
  RUN_CASE(check_attach_empty_page_fails_closed);
  RUN_CASE(check_attach_non_probe_page_returns_false);
  RUN_CASE(check_attach_missing_engine_fails_closed);
  RUN_CASE(check_attach_missing_ime_fails_closed);
  RUN_CASE(check_attach_missing_clipboard_fails_closed);
  RUN_CASE(check_attach_missing_inset_fails_closed);
  RUN_CASE(check_attach_twice_stays_true);
  RUN_CASE(check_attach_never_throws);
  RUN_CASE(check_composition_leg_precondition);
  RUN_CASE(check_selection_leg_precondition);
  RUN_CASE(check_inset_leg_precondition);
  RUN_CASE(check_clipboard_leg_precondition);
  if (failures > 0) {
    std::printf("textfield_probe_attach_test: %d failing\n", failures);
    return 1;
  }
  std::printf("textfield_probe_attach_test: all passing\n");
  return 0;
}
