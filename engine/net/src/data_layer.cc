#include "net/data_layer.h"

namespace muse::net {
namespace {

// Current schema stamped when a store is first opened. Nonzero so the T2
// version gate can distinguish "never opened" (0) from a live store.
constexpr uint64_t kCurrentSchemaVersion = 1;
// Rejects absurd inputs (e.g. the 64 KiB probe) without throwing.
constexpr std::size_t kMaxPathLength = 4096;

}  // namespace

DataLayer::DataLayer(DataLayerConfig cfg) : cfg_(std::move(cfg)) {}

bool DataLayer::open(const std::string& path) {
  if (path.empty() || path.size() > kMaxPathLength) {
    return false;
  }
  if (open_ && path == path_) {
    return true;
  }
  // Hermetic open: ":memory:" and logical path names resolve in memory; no
  // disk, no JS, no seam use on this path (null executor/keys tolerated).
  path_ = path;
  open_ = true;
  if (version_ == 0) {
    version_ = kCurrentSchemaVersion;
  }
  return true;
}

bool DataLayer::isOpen() const { return open_; }

uint64_t DataLayer::schemaVersion() const { return version_; }

}  // namespace muse::net
