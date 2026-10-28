#pragma once

#include <mutex>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Background-sync scheduling constraint (FR-STATE-005, AC-STATE-023). kAny
// runs under any network, kUnmetered waits for unmetered connectivity, kIdle
// additionally defers to an idle/Doze window via the services matrix.
enum class SyncConstraint {
  kAny = 0,
  kUnmetered = 1,
  kIdle = 2,
};

// Sync scheduling request. Aggregate { constraint, requiresDozeExemption }:
// requiresDozeExemption marks Doze/App-Standby-exempt work handed to the
// services background-execution matrix (WorkManager/JobScheduler). The real
// backend is out of scope; production schedules through the SyncScheduler
// seam and tests inject FakeScheduler, never a live process.
struct SyncRequest {
  SyncConstraint constraint = SyncConstraint::kAny;
  bool requiresDozeExemption = false;
};

// Scheduling seam mirroring the services background-execution matrix. Never
// touches a live process: the real Doze-aware backend lives behind this
// interface and is out of scope for this run.
class SyncScheduler {
 public:
  virtual ~SyncScheduler() = default;
  virtual Status schedule(SyncRequest r) = 0;
};

// In-process fake: records the most recent request and returns kOk for every
// constraint. Empty last() reports Status::kNotFound, never crashes.
// Thread-safe via one short-held member mutex, mirroring EpochStore.
class FakeScheduler : public SyncScheduler {
 public:
  FakeScheduler() = default;
  FakeScheduler(const FakeScheduler&) = delete;
  FakeScheduler& operator=(const FakeScheduler&) = delete;

  Status schedule(SyncRequest r) override;
  StatusOr<SyncRequest> last() const;

 private:
  mutable std::mutex mutex_;
  bool has_request_ = false;
  SyncRequest last_;
};

}  // namespace state
}  // namespace muse
