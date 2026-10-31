// T6 contract tests — FR-PVIEWS-010 / AC-PVIEWS-013 / TC-PVIEWS-013.
// Binds ONLY to the plan T6 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/embed.h + pviews/brownfield.h only; fake
// HostHandle/EngineHandle values, no Android View/Fragment/Activity classes,
// no manifest parser, no device, no network, no env/config reads,
// no filesystem, no live services. Errors are return values, never thrown:
// every error-path case also proves no exception escapes (try/catch).
//
// RED state (Batch 4, pre-coder): pviews/brownfield.h and the brownfield
// host registry in src/brownfield.cpp do not exist yet, so this file does
// NOT compile. That compile failure is the expected red for every case.
//
// ASSUMED header shape (see unit-tests/T6.md; coder must satisfy it;
// brownfield.h reuses HostHandle/EngineHandle from pviews/embed.h and must
// NOT redefine them):
//   enum class HostKind { VIEW = 0, FRAGMENT = 1, ACTIVITY = 2 };
//   enum class HostMode { SHARED = 0, MULTI = 1 };
//   enum class HostErrorCode {
//     NONE = 0, HOST_BAD_MODE = 1, HOST_NO_ENGINE = 2
//   };
//   struct HostCreateResult {
//     bool ok = false; HostHandle handle{}; HostErrorCode error = NONE;
//   };
//   HostCreateResult createHost(HostKind kind, HostMode mode,
//                              EngineHandle engine) noexcept;
//   void destroyHost(HostHandle h) noexcept;
//   bool hostIsLive(HostHandle h) noexcept;
//   EngineHandle hostEngineOf(HostHandle h) noexcept;
// Semantics assumed: Shared mode requires a live engine (null engine ->
// HOST_NO_ENGINE); Multi mode with a null engine mints a host-owned engine
// (fallback path); out-of-range kind/mode -> HOST_BAD_MODE; destroyHost on
// an unknown/already-destroyed handle is a safe no-op.
#include <cstdint>
#include <cstdio>

#include "pviews/brownfield.h"
#include "pviews/embed.h"

// Case: EngineView host in shared mode binds the supplied engine.
static bool check_create_view_shared_ok() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{101}, EmbedOpts{});
  if (!e.ok) return false;
  const HostCreateResult r = createHost(HostKind::VIEW, HostMode::SHARED,
                                        e.handle);
  return r.ok && r.handle.value != 0 && r.error == HostErrorCode::NONE &&
         hostIsLive(r.handle) &&
         hostEngineOf(r.handle).value == e.handle.value;
}

// Case: Fragment host in multi mode with no engine mints a host-owned
// engine (fallback path) and is live.
static bool check_create_fragment_multi_null_engine_ok() {
  const HostCreateResult r = createHost(HostKind::FRAGMENT, HostMode::MULTI,
                                        EngineHandle{0});
  return r.ok && r.handle.value != 0 && r.error == HostErrorCode::NONE &&
         hostIsLive(r.handle) &&
         hostEngineOf(r.handle).value != 0;
}

// Case: Activity hosts construct in both modes.
static bool check_create_activity_both_modes_ok() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{102}, EmbedOpts{});
  if (!e.ok) return false;
  const HostCreateResult s = createHost(HostKind::ACTIVITY, HostMode::SHARED,
                                        e.handle);
  const HostCreateResult m = createHost(HostKind::ACTIVITY, HostMode::MULTI,
                                        EngineHandle{0});
  return s.ok && m.ok && s.handle.value != m.handle.value &&
         hostIsLive(s.handle) && hostIsLive(m.handle);
}

// Case: shared mode with a null engine reports HOST_NO_ENGINE as a return
// value — never throws.
static bool check_shared_null_engine_no_engine() {
  for (int k = 0; k < 3; ++k) {
    try {
      const HostCreateResult r =
          createHost(static_cast<HostKind>(k), HostMode::SHARED,
                     EngineHandle{0});
      if (r.ok || r.handle.value != 0 ||
          r.error != HostErrorCode::HOST_NO_ENGINE)
        return false;
    } catch (...) {
      return false;  // errors are values, never exceptions
    }
  }
  return true;
}

// Case: out-of-range kind reports HOST_BAD_MODE — never throws.
static bool check_bad_kind_bad_mode() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{103}, EmbedOpts{});
  if (!e.ok) return false;
  try {
    const HostCreateResult r = createHost(static_cast<HostKind>(99),
                                          HostMode::SHARED, e.handle);
    return !r.ok && r.handle.value == 0 &&
           r.error == HostErrorCode::HOST_BAD_MODE;
  } catch (...) {
    return false;
  }
}

// Case: out-of-range mode reports HOST_BAD_MODE — never throws.
static bool check_bad_mode_bad_mode() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{104}, EmbedOpts{});
  if (!e.ok) return false;
  try {
    const HostCreateResult r = createHost(HostKind::VIEW,
                                          static_cast<HostMode>(99),
                                          e.handle);
    return !r.ok && r.handle.value == 0 &&
           r.error == HostErrorCode::HOST_BAD_MODE;
  } catch (...) {
    return false;
  }
}

// Case: multi-engine mode isolates UI state — two hosts own different
// engines.
static bool check_multi_hosts_isolated_engines() {
  const HostCreateResult a = createHost(HostKind::FRAGMENT, HostMode::MULTI,
                                        EngineHandle{0});
  const HostCreateResult b = createHost(HostKind::FRAGMENT, HostMode::MULTI,
                                        EngineHandle{0});
  if (!a.ok || !b.ok) return false;
  return hostEngineOf(a.handle).value != 0 &&
         hostEngineOf(b.handle).value != 0 &&
         hostEngineOf(a.handle).value != hostEngineOf(b.handle).value;
}

// Case: shared mode shares one engine across hosts.
static bool check_shared_hosts_share_engine() {
  const EngineEmbedResult e = embedEngineInView(HostHandle{105}, EmbedOpts{});
  if (!e.ok) return false;
  const HostCreateResult a = createHost(HostKind::VIEW, HostMode::SHARED,
                                        e.handle);
  const HostCreateResult b = createHost(HostKind::FRAGMENT, HostMode::SHARED,
                                        e.handle);
  if (!a.ok || !b.ok) return false;
  return hostEngineOf(a.handle).value == e.handle.value &&
         hostEngineOf(b.handle).value == e.handle.value;
}

// Case: destroyHost releases the host; a destroyed handle is not live and
// double-destroy is a safe no-op.
static bool check_destroy_releases_host() {
  const HostCreateResult r = createHost(HostKind::VIEW, HostMode::MULTI,
                                        EngineHandle{0});
  if (!r.ok || !hostIsLive(r.handle)) return false;
  destroyHost(r.handle);
  if (hostIsLive(r.handle)) return false;
  destroyHost(r.handle);  // must not crash or throw
  return !hostIsLive(r.handle);
}

// Case: host setup is config-free (manifest-clean proxy) — repeated
// create/destroy cycles need no extra setup and leave no residue: every
// cycle yields a fresh live host.
static bool check_create_destroy_cycles_need_no_setup() {
  for (int i = 0; i < 3; ++i) {
    const HostCreateResult r = createHost(HostKind::ACTIVITY, HostMode::MULTI,
                                          EngineHandle{0});
    if (!r.ok || !hostIsLive(r.handle)) return false;
    destroyHost(r.handle);
    if (hostIsLive(r.handle)) return false;
  }
  return true;
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
  RUN_CASE(check_create_view_shared_ok);
  RUN_CASE(check_create_fragment_multi_null_engine_ok);
  RUN_CASE(check_create_activity_both_modes_ok);
  RUN_CASE(check_shared_null_engine_no_engine);
  RUN_CASE(check_bad_kind_bad_mode);
  RUN_CASE(check_bad_mode_bad_mode);
  RUN_CASE(check_multi_hosts_isolated_engines);
  RUN_CASE(check_shared_hosts_share_engine);
  RUN_CASE(check_destroy_releases_host);
  RUN_CASE(check_create_destroy_cycles_need_no_setup);
  if (failures > 0) {
    std::printf("brownfield_host_test: %d failing\n", failures);
    return 1;
  }
  std::printf("brownfield_host_test: all passing\n");
  return 0;
}
