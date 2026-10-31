// T1 contract tests — FR-PVIEWS-006 / AC-PVIEWS-008 / TC-PVIEWS-008.
// Binds ONLY to the plan T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/hybrid_embedding.h (+ geometry shapes) only;
// fake SdkViewId/ClipRect/Transform values, no Activity, no window, no
// HardwareRenderer/VkImage, no device, no network, no env/config reads,
// no filesystem, no live services.
#include <cstdio>

#include "pviews/hybrid_embedding.h"
#include "pviews/z_order.h"

// Case: AdMob-style SDK id attaches inside layout bounds.
static bool check_admob_attach_placed_clipped_pooled_exported() {
  HybridPool pool;
  SdkViewId id{101};  // fake AdMob-style SDK view id
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  const HybridResult r =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return r.placed && r.clipApplied && r.pooled && r.exported &&
         r.error == HybridError::None;
}

// Case: banking-style SDK id attaches alongside the first entry.
static bool check_banking_attach_placed_clipped_pooled_exported() {
  HybridPool pool;
  SdkViewId admob{101};
  SdkViewId bank{202};  // fake banking-style SDK view id
  ClipRect clipA{0, 0, 320, 50, 0};
  ClipRect clipB{0, 60, 320, 260, 8};
  Transform t = Transform::identity();
  const HybridResult a = attachHybridView(pool, admob, clipA, t,
                                          ExportCapability::ExportCapable);
  const HybridResult b =
      attachHybridView(pool, bank, clipB, t, ExportCapability::ExportCapable);
  return a.placed && b.placed && b.clipApplied && b.pooled && b.exported &&
         b.error == HybridError::None && b.layer != -1;
}

// Case: invalid geometry (empty clip rect) reports the error shape.
static bool check_invalid_geometry_error_shape() {
  HybridPool pool;
  SdkViewId id{101};
  ClipRect clip{10, 10, 5, 5, 0};  // right < left -> invalid
  Transform t = Transform::identity();
  const HybridResult r =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return !r.placed && r.layer == -1 && !r.clipApplied && !r.pooled &&
         !r.exported && r.error == HybridError::InvalidGeometry;
}

// Case: zero-area clip rect is invalid -> error shape.
static bool check_zero_area_clip_invalid() {
  HybridPool pool;
  SdkViewId id{101};
  ClipRect clip{0, 0, 0, 0, 0};
  Transform t = Transform::identity();
  const HybridResult r =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return !r.placed && r.layer == -1 && !r.clipApplied && !r.pooled &&
         !r.exported && r.error == HybridError::InvalidGeometry;
}

// Case: invalid SDK view id (0) reports the error shape.
static bool check_invalid_id_zero_error_shape() {
  HybridPool pool;
  SdkViewId id{0};
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  const HybridResult r =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return !r.placed && r.layer == -1 && !r.clipApplied && !r.pooled &&
         !r.exported && r.error == HybridError::InvalidGeometry;
}

// Case: non-exporting SDK (cap == NoExport) escalates to T2 fallback.
static bool check_noexport_unsupported_shape() {
  HybridPool pool;
  SdkViewId id{303};  // fake non-exporting SDK view id
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  const HybridResult r =
      attachHybridView(pool, id, clip, t, ExportCapability::NoExport);
  return !r.placed && r.layer == -1 && !r.clipApplied && !r.pooled &&
         !r.exported && r.error == HybridError::ExportUnsupported;
}

// Case: pool full denies fail-closed with PoolExhausted (cap-agnostic:
// attach until the fixed cap denies; bound the loop so no hang).
static bool check_pool_full_exhausted_shape() {
  HybridPool pool;
  ClipRect clip{0, 0, 100, 100, 0};
  Transform t = Transform::identity();
  bool sawExhausted = false;
  for (std::uint64_t i = 1; i <= 256; ++i) {
    SdkViewId id{i};
    const HybridResult r =
        attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
    if (!r.placed && r.error == HybridError::PoolExhausted) {
      sawExhausted = r.layer == -1 && !r.clipApplied && !r.pooled &&
                     !r.exported;
      break;
    }
    if (!r.placed) {
      return false;  // denied for a reason other than PoolExhausted
    }
  }
  return sawExhausted;
}

// Case: reclaimAll on lifecycle stop empties the pool; re-attach succeeds.
static bool check_reclaim_all_then_reattach() {
  HybridPool pool;
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  SdkViewId id{101};
  const HybridResult first =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  if (!first.placed) {
    return false;
  }
  reclaimAll(pool);
  const HybridResult second =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return second.placed && second.clipApplied && second.pooled &&
         second.exported && second.error == HybridError::None;
}

// Case: reclaimAll is idempotent (double reclaim is safe).
static bool check_reclaim_all_idempotent() {
  HybridPool pool;
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  SdkViewId id{101};
  const HybridResult first =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  if (!first.placed) {
    return false;
  }
  reclaimAll(pool);
  reclaimAll(pool);
  const HybridResult second =
      attachHybridView(pool, id, clip, t, ExportCapability::ExportCapable);
  return second.placed && second.error == HybridError::None;
}

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
  RUN_CASE(check_admob_attach_placed_clipped_pooled_exported);
  RUN_CASE(check_banking_attach_placed_clipped_pooled_exported);
  RUN_CASE(check_invalid_geometry_error_shape);
  RUN_CASE(check_zero_area_clip_invalid);
  RUN_CASE(check_invalid_id_zero_error_shape);
  RUN_CASE(check_noexport_unsupported_shape);
  RUN_CASE(check_pool_full_exhausted_shape);
  RUN_CASE(check_reclaim_all_then_reattach);
  RUN_CASE(check_reclaim_all_idempotent);
  if (failures > 0) {
    std::printf("hybrid_embedding_test: %d failing\n", failures);
    return 1;
  }
  std::printf("hybrid_embedding_test: all passing\n");
  return 0;
}
