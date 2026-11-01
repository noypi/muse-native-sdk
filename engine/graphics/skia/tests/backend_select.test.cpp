// Batch 1 contract test — T1 (FR-GRAPHICS-016 / AC-GRAPHICS-045).
// Binds ONLY to the T1 `Interface:` line from plan.md; never to implementation.
// Hermetic: C++17 + `probe_skia.h` only; no Skia SDK calls on host, no GPU
// context, no network, no env/config reads, no filesystem, no live services.
// Follows tests/select_present.test.cpp convention (plain main, RUN_CASE,
// PASS/FAIL, nonzero on failure).
//
// Expected red: this TU does not compile until the coder publishes the
// single-variant enum + selector in `engine/graphics/skia/probe_skia.h`
// (+ probe_skia.cpp) with the exact plan interface
// (`probe_skia.h: No such file or directory` is NOT the failure — the header
// exists; the missing piece is `SkiaBackendVariant` /
// `SelectSkiaBackendVariant`).
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "probe_skia.h"

// --- T1 public interface (plan verbatim) --------------------------------------
// `SkiaBackendVariant SelectSkiaBackendVariant(const SkiaProbeInput& input)
// noexcept` where `enum class SkiaBackendVariant { kGraphite };` — returns
// `kGraphite` whenever `ProbeSkia(input).selected` is true; error shape:
// never throws, no sentinel needed (unselected input yields `kGraphite`
// pending-selection value; callers must still gate on `ProbeSkia().selected`).

static bool check_enum_has_kGraphite() {
  SkiaBackendVariant v = SkiaBackendVariant::kGraphite;
  (void)v;
  return true;
}

static bool check_signature_matches_plan() {
  static_assert(std::is_same<decltype(&SelectSkiaBackendVariant),
                             SkiaBackendVariant (*)(const SkiaProbeInput&) noexcept>::value,
                "SelectSkiaBackendVariant must be "
                "SkiaBackendVariant (const SkiaProbeInput&) noexcept");
  static_assert(noexcept(SelectSkiaBackendVariant(SkiaProbeInput{})),
                "SelectSkiaBackendVariant must be noexcept");
  return true;
}

static bool check_selected_input_returns_kGraphite() {
  SkiaProbeInput in{true, true};
  if (!ProbeSkia(in).selected) return false;  // precondition for this case
  return SelectSkiaBackendVariant(in) == SkiaBackendVariant::kGraphite;
}

static bool check_unselected_input_yields_pending_kGraphite() {
  // Unselected input yields `kGraphite` as a pending-selection value;
  // callers must still gate on `ProbeSkia().selected`.
  const SkiaProbeInput cases[] = {{false, true}, {true, false}, {false, false}};
  for (const auto& in : cases) {
    if (ProbeSkia(in).selected) return false;  // precondition for this case
    if (SelectSkiaBackendVariant(in) != SkiaBackendVariant::kGraphite) {
      return false;
    }
  }
  return true;
}

static bool check_never_throws_selected() {
  SkiaProbeInput in{true, true};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (void)SelectSkiaBackendVariant(in);
  } catch (...) {
    return false;
  }
  return true;
#else
  (void)SelectSkiaBackendVariant(in);
  return true;
#endif
}

static bool check_never_throws_unselected() {
  SkiaProbeInput in{false, false};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (void)SelectSkiaBackendVariant(in);
  } catch (...) {
    return false;
  }
  return true;
#else
  (void)SelectSkiaBackendVariant(in);
  return true;
#endif
}

static bool check_result_is_deterministic() {
  SkiaProbeInput in{true, true};
  return SelectSkiaBackendVariant(in) == SelectSkiaBackendVariant(in);
}

// --- harness (convention copied from tests/select_present.test.cpp) -----------

static bool check_harness_entry_reached() { return true; }

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_enum_has_kGraphite);
  RUN_CASE(check_signature_matches_plan);
  RUN_CASE(check_selected_input_returns_kGraphite);
  RUN_CASE(check_unselected_input_yields_pending_kGraphite);
  RUN_CASE(check_never_throws_selected);
  RUN_CASE(check_never_throws_unselected);
  RUN_CASE(check_result_is_deterministic);
  if (failures > 0) {
    std::printf("backend_select: %d failing\n", failures);
    return 1;
  }
  std::printf("backend_select: all passing\n");
  return 0;
}
