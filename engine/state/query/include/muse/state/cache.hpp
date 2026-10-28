#pragma once

#include <utility>
#include <vector>

#include "muse/state/query_client.hpp"
#include "muse/state/retry_policy.hpp"

namespace muse {
namespace state {

// Seeded-entry list used by rehydrate (T7, FR-STATE-004/AC-STATE-020).
// This is the tester-chosen Batch 3 shape: each pair seeds one key with a
// fresh-but-stale value. It is deliberately NOT the versioned persist
// Snapshot Batch 4 owns (`engine/state/persist/.../snapshot.hpp`): if Batch 4
// settles a different Snapshot type, converge on one shared spelling or add
// an explicit conversion there — do not redeclare muse::state::Snapshot here.
using Snapshot = std::vector<std::pair<QueryKey, Value>>;

enum class StalePolicy {
  kStaleWithRefetch = 0,
};

// Stale-while-revalidate surface (TC-STATE-020 end-to-end):
// - rehydrate loads fresh-but-stale entries (immediately readable via
//   readStaleWhileRevalidate) and marks them for refetch per policy. The
//   actual refetch is observed on the next QueryClient::fetch; rehydrate
//   itself never issues transport calls, so an empty snapshot is a kOk
//   no-op and rehydrated entries can never overwrite live QueryClient state.
// - readStaleWhileRevalidate returns the stale value synchronously (the
//   refetch is scheduled, not awaited); unknown keys yield a default Value.
// - backoffCurve exposes the policy's per-retry delays verbatim for the
//   fault-injection assertion (cumulative pairs derived by summation).
Status rehydrate(Snapshot s, StalePolicy p);
Value readStaleWhileRevalidate(QueryKey k);
BackoffCurve backoffCurve(const RetryPolicy& p);

}  // namespace state
}  // namespace muse
