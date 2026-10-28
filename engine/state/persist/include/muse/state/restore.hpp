#pragma once

#include "muse/state/corruption.hpp"
#include "muse/state/snapshot.hpp"

namespace muse {
namespace state {
namespace persist {

// Non-store slots are opaque fixtures (byte-vector stand-ins).
using StoreData = Bytes;
using NavState = Bytes;
using ScrollState = Bytes;

struct FocusState {
  bool valid = false;
};

// Security-privacy hook (incidental, nullable at call sites). Real crypto is
// external; tests substitute an identity fake.
class FieldDecryptor {
 public:
  virtual ~FieldDecryptor() = default;
  virtual Bytes decrypt(const Bytes&) = 0;
};

struct AppState {
  StoreData stores;
  NavState nav;
  ScrollState scroll;
  FocusState focus;
  // Test-convenience mirror of focus.valid (tests read `a.focusValid`).
  bool focusValid = false;
};

// Process-death restore of stores/nav/scroll/focus with migration and
// bit-flip rejection. Clean (checksum-verified, migrated) snapshots populate
// all four slots; tampered input fails closed with Status::kCorrupt (+
// exactly one kSnapshotCorrupt sink event via loadChecked) and no value.
// sink and d are both nullable and never crash when null.
StatusOr<AppState> restoreAfterDeath(Snapshot s, const MigrationRegistry& r,
                                     TelemetrySink* sink, FieldDecryptor* d);

}  // namespace persist
}  // namespace state
}  // namespace muse
