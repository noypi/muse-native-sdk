#pragma once

#include "muse/state/snapshot.hpp"

namespace muse {
namespace state {
namespace persist {

// Reliability-observability seam (incidental consumer, faked in tests).
// Nullable at every call site: loadChecked(s, nullptr) must not crash.
enum class TelemetryEvent { kSnapshotCorrupt = 0 };

class TelemetrySink {
 public:
  virtual ~TelemetrySink() = default;
  virtual void emit(TelemetryEvent) = 0;
};

// Degrade-to-defaults outcome (FR-STATE-007, AC-STATE-030). `event` is
// meaningful only when `clean == false` (always kSnapshotCorrupt there);
// the clean path emits nothing to the sink.
struct RestoreOutcome {
  bool clean;
  TelemetryEvent event;
};

// Verifies s.checksum against FNV-1a-32 over s.payload (checksumPayload).
// Clean -> {true, ...} with no sink event. Any mismatch (bit-flip,
// truncated / wrong-length payload, corrupt checksum field) -> {false,
// kSnapshotCorrupt} plus exactly one kSnapshotCorrupt emission when sink is
// non-null. Never throws; never aborts startup.
RestoreOutcome loadChecked(Snapshot s, TelemetrySink* sink) noexcept;

}  // namespace persist
}  // namespace state
}  // namespace muse
