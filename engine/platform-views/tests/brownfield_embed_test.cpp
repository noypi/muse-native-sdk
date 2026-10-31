// T1 contract tests — FR-PVIEWS-003 / AC-PVIEWS-004 / TC-PVIEWS-004.
// Binds ONLY to the plan T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/embed.h only; fake HostHandle/EngineHandle/
// ViewHandle/Rect/EmbedOpts values, no Android View/Compose classes, no
// device, no network, no env/config reads, no filesystem, no live services.
// Errors are return values, never thrown: every error-path case also proves
// no exception escapes (try/catch around the call).
//
// RED state (Batch 1, pre-coder): pviews/embed.h and src/embed.cpp do not
// exist yet, so this file does NOT compile. That compile failure is the
// expected red for every case below.
//
// ASSUMED header shape (see unit-tests/T1.md; coder must satisfy it):
//   struct HostHandle { uint64_t value; };   // 0 == invalid / null host
//   struct EngineHandle { uint64_t value; }; // 0 == invalid
//   struct ViewHandle { uint64_t value; };   // 0 == invalid
//   struct Rect { int left, top, right, bottom; };
//   struct EmbedOpts {};
//   struct ViewSlot { ViewHandle view; Rect bounds; };
//   enum class EmbedErrorCode {
//     NONE = 0, EMBED_ALREADY_ATTACHED = 1, EMBED_BAD_HOST = 2
//   };
//   struct EngineEmbedResult {
//     bool ok; EngineHandle handle; EmbedErrorCode error;
//   };
//   struct ViewSlotResult { bool ok; ViewSlot slot; EmbedErrorCode error; };
//   EngineEmbedResult embedEngineInView(HostHandle host,
//                                       EmbedOpts opts) noexcept;
//   ViewSlotResult embedViewInEngine(EngineHandle engine, ViewHandle view,
//                                    Rect bounds) noexcept;
#include <cstdint>
#include <cstdio>

#include "pviews/embed.h"

// Case: engine embeds into a valid host view hierarchy -> ok + live handle.
static bool check_engine_into_valid_host_ok() {
  HostHandle host{1};
  EmbedOpts opts{};
  const EngineEmbedResult r = embedEngineInView(host, opts);
  return r.ok && r.handle.value != 0 &&
         r.error == EmbedErrorCode::NONE;
}

// Case: a platform view embeds into the engine with layout bounds ->
// ok + slot echoes the view and bounds.
static bool check_view_into_engine_slot_bounds_match() {
  HostHandle host{2};
  const EngineEmbedResult e = embedEngineInView(host, EmbedOpts{});
  if (!e.ok) return false;
  ViewHandle view{10};
  Rect bounds{0, 0, 100, 60};
  const ViewSlotResult s = embedViewInEngine(e.handle, view, bounds);
  return s.ok && s.slot.view.value == view.value &&
         s.slot.bounds.left == 0 && s.slot.bounds.top == 0 &&
         s.slot.bounds.right == 100 && s.slot.bounds.bottom == 60 &&
         s.error == EmbedErrorCode::NONE;
}

// Case: attaching a second engine to the same host reports
// EMBED_ALREADY_ATTACHED as a return value — never throws.
static bool check_double_engine_attach_already_attached() {
  HostHandle host{3};
  const EngineEmbedResult first = embedEngineInView(host, EmbedOpts{});
  if (!first.ok) return false;
  try {
    const EngineEmbedResult second = embedEngineInView(host, EmbedOpts{});
    return !second.ok &&
           second.error == EmbedErrorCode::EMBED_ALREADY_ATTACHED;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

// Case: null/invalid host reports EMBED_BAD_HOST — never throws.
static bool check_null_host_bad_host() {
  HostHandle nullHost{0};
  try {
    const EngineEmbedResult r = embedEngineInView(nullHost, EmbedOpts{});
    return !r.ok && r.handle.value == 0 &&
           r.error == EmbedErrorCode::EMBED_BAD_HOST;
  } catch (...) {
    return false;
  }
}

// Case: invalid view / degenerate bounds rejected with a typed error,
// never throws. ASSUMPTION: zero-area bounds map to EMBED_BAD_HOST.
static bool check_zero_area_bounds_error() {
  HostHandle host{4};
  const EngineEmbedResult e = embedEngineInView(host, EmbedOpts{});
  if (!e.ok) return false;
  ViewHandle view{11};
  Rect empty{10, 10, 5, 5};  // right < left -> invalid
  try {
    const ViewSlotResult s = embedViewInEngine(e.handle, view, empty);
    return !s.ok && s.error == EmbedErrorCode::EMBED_BAD_HOST;
  } catch (...) {
    return false;
  }
}

// Case: invalid view handle (0) into a valid engine is a typed denial.
static bool check_invalid_view_handle_error() {
  HostHandle host{5};
  const EngineEmbedResult e = embedEngineInView(host, EmbedOpts{});
  if (!e.ok) return false;
  ViewHandle bad{0};
  Rect bounds{0, 0, 50, 50};
  try {
    const ViewSlotResult s = embedViewInEngine(e.handle, bad, bounds);
    return !s.ok && s.error == EmbedErrorCode::EMBED_BAD_HOST;
  } catch (...) {
    return false;
  }
}

// Case: host app unmodified (no fork) — the host handle value is untouched
// by the embed, and a second independent host attaches cleanly.
static bool check_host_handle_untouched_no_fork() {
  HostHandle hostA{6};
  HostHandle hostB{7};
  const EngineEmbedResult a = embedEngineInView(hostA, EmbedOpts{});
  if (!a.ok) return false;
  if (hostA.value != 6) return false;  // embed must not mutate the host
  const EngineEmbedResult b = embedEngineInView(hostB, EmbedOpts{});
  return b.ok && b.handle.value != 0 &&
         b.handle.value != a.handle.value;
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_engine_into_valid_host_ok);
  RUN_CASE(check_view_into_engine_slot_bounds_match);
  RUN_CASE(check_double_engine_attach_already_attached);
  RUN_CASE(check_null_host_bad_host);
  RUN_CASE(check_zero_area_bounds_error);
  RUN_CASE(check_invalid_view_handle_error);
  RUN_CASE(check_host_handle_untouched_no_fork);
  if (failures > 0) {
    std::printf("brownfield_embed_test: %d failing\n", failures);
    return 1;
  }
  std::printf("brownfield_embed_test: all passing\n");
  return 0;
}
