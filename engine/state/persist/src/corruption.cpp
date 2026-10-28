#include "muse/state/corruption.hpp"

namespace muse {
namespace state {
namespace persist {

RestoreOutcome loadChecked(Snapshot s, TelemetrySink* sink) noexcept {
  try {
    if (checksumPayload(s.payload) == s.checksum) {
      return RestoreOutcome{true, TelemetryEvent::kSnapshotCorrupt};
    }
    // Corrupt: degrade to defaults, report exactly one fixed event with no
    // payload echo. Null sink is safe (startup path never crashes).
    if (sink != nullptr) sink->emit(TelemetryEvent::kSnapshotCorrupt);
    return RestoreOutcome{false, TelemetryEvent::kSnapshotCorrupt};
  } catch (...) {
    // Never throws, never aborts startup: fail closed to defaults.
    try {
      if (sink != nullptr) sink->emit(TelemetryEvent::kSnapshotCorrupt);
    } catch (...) {
    }
    return RestoreOutcome{false, TelemetryEvent::kSnapshotCorrupt};
  }
}

}  // namespace persist
}  // namespace state
}  // namespace muse
