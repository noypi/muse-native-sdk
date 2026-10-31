// T7 contract tests — FR-PVIEWS-010 / AC-PVIEWS-014 / TC-PVIEWS-014.
// Binds ONLY to the plan T7 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/embed.h + pviews/brownfield.h only; engines are
// minted via embedEngineInView (T1, done) so no Android runtime, no device,
// no network, no env/config reads, no filesystem, no live services.
// Errors are return values, never thrown: every error-path case also proves
// no exception escapes (try/catch).
//
// RED state (Batch 4, pre-coder): the shared-engine refcount registry in
// src/brownfield.cpp (retainEngine/releaseOnLastDetach) does not exist yet,
// so this file does NOT compile. That compile failure is the expected red
// for every case below.
//
// ASSUMED header shape (see unit-tests/T7.md; coder must satisfy it; the
// T6 brownfield.h owns these declarations, T7 adds no new header):
//   using RefCount = int;
//   enum class EngineTeardownCode {
//     RELEASED = 0, STILL_HELD = 1, ENGINE_DOUBLE_RELEASE = 2
//   };
//   struct EngineReleaseResult {
//     bool released = false; int remaining = 0;
//     EngineTeardownCode code = EngineTeardownCode::STILL_HELD;
//   };
//   RefCount retainEngine(EngineHandle e) noexcept;
//   EngineReleaseResult releaseOnLastDetach(EngineHandle e) noexcept;
// Semantics assumed: an engine minted by embedEngineInView enters the
// registry with count 1; retainEngine returns the new count (0 for an
// unknown/invalid engine); releaseOnLastDetach decrements — count 0 ->
// {true, 0, RELEASED}; count > 0 -> {false, n, STILL_HELD}; unknown or
// already-released engine -> {false, 0, ENGINE_DOUBLE_RELEASE}, never
// throws, never crashes.
#include <cstdint>
#include <cstdio>

#include "pviews/brownfield.h"
#include "pviews/embed.h"

// Case: retainEngine increments the refcount and returns the new count.
static bool check_retain_increments_count() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{201}, EmbedOpts{});
  if (!e.ok) return false;
  const RefCount c1 = retainEngine(e.handle);
  const RefCount c2 = retainEngine(e.handle);
  return c1 == 2 && c2 == 3;
}

// Case: releasing while holders remain reports StillHeld with the
// remaining count and does not release.
static bool check_release_with_holders_still_held() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{202}, EmbedOpts{});
  if (!e.ok) return false;
  retainEngine(e.handle);  // count 2
  retainEngine(e.handle);  // count 3
  const EngineReleaseResult r = releaseOnLastDetach(e.handle);
  return !r.released && r.remaining == 2 &&
         r.code == EngineTeardownCode::STILL_HELD;
}

// Case: detaching the last holder releases the engine.
static bool check_last_detach_releases() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{203}, EmbedOpts{});
  if (!e.ok) return false;
  const EngineReleaseResult r = releaseOnLastDetach(e.handle);
  return r.released && r.remaining == 0 &&
         r.code == EngineTeardownCode::RELEASED;
}

// Case: two hosts share one engine (one retain each); releasing the first
// still holds, releasing the last releases.
static bool check_two_hosts_last_detach_releases() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{204}, EmbedOpts{});
  if (!e.ok) return false;
  const HostCreateResult a = createHost(HostKind::VIEW, HostMode::SHARED,
                                        e.handle);
  const HostCreateResult b = createHost(HostKind::FRAGMENT, HostMode::SHARED,
                                        e.handle);
  if (!a.ok || !b.ok) return false;
  retainEngine(e.handle);  // host a
  retainEngine(e.handle);  // host b -> count 3
  destroyHost(a.handle);
  const EngineReleaseResult first = releaseOnLastDetach(e.handle);
  if (first.released || first.code != EngineTeardownCode::STILL_HELD)
    return false;
  destroyHost(b.handle);
  const EngineReleaseResult last = releaseOnLastDetach(e.handle);
  // One engine-owner reference remains from embed; release it too.
  const EngineReleaseResult owner = releaseOnLastDetach(e.handle);
  return !last.released && last.code == EngineTeardownCode::STILL_HELD &&
         owner.released && owner.remaining == 0 &&
         owner.code == EngineTeardownCode::RELEASED;
}

// Case: over-release after a full release reports ENGINE_DOUBLE_RELEASE —
// never throws, never crashes.
static bool check_double_release_error_not_crash() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{205}, EmbedOpts{});
  if (!e.ok) return false;
  const EngineReleaseResult first = releaseOnLastDetach(e.handle);
  if (!first.released) return false;
  try {
    const EngineReleaseResult second = releaseOnLastDetach(e.handle);
    const EngineReleaseResult third = releaseOnLastDetach(e.handle);
    return !second.released && second.remaining == 0 &&
           second.code == EngineTeardownCode::ENGINE_DOUBLE_RELEASE &&
           !third.released && third.remaining == 0 &&
           third.code == EngineTeardownCode::ENGINE_DOUBLE_RELEASE;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

// Case: releasing an engine that was never registered reports
// ENGINE_DOUBLE_RELEASE — never throws.
static bool check_release_unknown_engine_double_release() {
  try {
    const EngineReleaseResult r =
        releaseOnLastDetach(EngineHandle{0xDEAD});
    return !r.released && r.remaining == 0 &&
           r.code == EngineTeardownCode::ENGINE_DOUBLE_RELEASE;
  } catch (...) {
    return false;
  }
}

// Case: retaining an invalid/unknown engine returns 0 and never throws.
static bool check_retain_invalid_engine_zero() {
  try {
    return retainEngine(EngineHandle{0}) == 0 &&
           retainEngine(EngineHandle{0xBEEF}) == 0;
  } catch (...) {
    return false;
  }
}

// Case: released engines stay released — a further retain on a fully
// released engine returns 0 (no resurrection).
static bool check_no_resurrection_after_release() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{206}, EmbedOpts{});
  if (!e.ok) return false;
  const EngineReleaseResult rel = releaseOnLastDetach(e.handle);
  if (!rel.released) return false;
  return retainEngine(e.handle) == 0;
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
  RUN_CASE(check_retain_increments_count);
  RUN_CASE(check_release_with_holders_still_held);
  RUN_CASE(check_last_detach_releases);
  RUN_CASE(check_two_hosts_last_detach_releases);
  RUN_CASE(check_double_release_error_not_crash);
  RUN_CASE(check_release_unknown_engine_double_release);
  RUN_CASE(check_retain_invalid_engine_zero);
  RUN_CASE(check_no_resurrection_after_release);
  if (failures > 0) {
    std::printf("shared_engine_teardown_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shared_engine_teardown_test: all passing\n");
  return 0;
}
