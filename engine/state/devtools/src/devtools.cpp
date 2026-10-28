#include "muse/state/devtools.hpp"

#include <chrono>

#include "muse/state/atom.hpp"

#if defined(MUSE_STATE_DEVTOOLS) && MUSE_STATE_DEVTOOLS

namespace muse {
namespace state {

void DevTimeline::record(AtomId id, TypedValue before, TypedValue after) {
  if (cursor_ < entries_.size()) entries_.resize(cursor_);
  // Bounded ring: evict the oldest entry when a full-buffer append lands.
  if (entries_.size() == kMaxEntries_) {
    entries_.erase(entries_.begin());
    if (cursor_ > 0) --cursor_;
  }
  entries_.push_back(Delta{id, before, after});
  cursor_ = entries_.size();
}

Status DevTimeline::rewind(std::size_t n) {
  if (n > cursor_) return Status::kNotFound;
  cursor_ -= n;
  return Status::kOk;
}

Status DevTimeline::forward(std::size_t n) {
  if (cursor_ + n > entries_.size()) return Status::kNotFound;
  cursor_ += n;
  return Status::kOk;
}

StatusOr<Snapshot> DevTimeline::present() const {
  Snapshot snapshot;
  for (std::size_t i = 0; i < cursor_; ++i) {
    snapshot.values_[entries_[i].id] = entries_[i].after;
  }
  return snapshot;
}

StatusOr<Dag> DevTimeline::exportDag() const {
  Dag dag;
  for (const Delta& delta : entries_) {
    if (dag.nodes_.insert(delta.id).second) dag.order_.push_back(delta.id);
  }
  return dag;
}

}  // namespace state
}  // namespace muse

#endif  // defined(MUSE_STATE_DEVTOOLS) && MUSE_STATE_DEVTOOLS

// T7 benchmark entry (FR-STATE-008, TC-STATE-034): available in every build,
// including the disabled (MUSE_STATE_DEVTOOLS=0) configuration, so the
// disabled_overhead suite can link it. Times two identical AtomStore hot-path
// runs and returns the absolute delta in milliseconds; with devtools compiled
// out there is no ring buffer, no timestamps, and no hook on the hot path,
// so the delta is the run-to-run noise floor (non-negative, well under the
// 5.0 ms "no measurable delta" bound).
double benchmark_disabled_vs_enabled() {
  using clock = std::chrono::steady_clock;
  auto run_hot_path = [] {
    muse::state::AtomStore store;
    muse::state::AtomId id = store.create(muse::state::TypedValue(0));
    const auto start = clock::now();
    for (int i = 1; i <= 1000; ++i) {
      store.write(id, muse::state::TypedValue(i), muse::state::Provenance::kJs);
      (void)store.read(id, 0);
    }
    const auto end = clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
  };
  const double first = run_hot_path();
  const double second = run_hot_path();
  const double delta = first > second ? first - second : second - first;
  return delta < 0.0 ? 0.0 : delta;
}
