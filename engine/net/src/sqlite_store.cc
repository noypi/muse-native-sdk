#include "net/sqlite_store.h"

namespace muse::net {

OpenResult OpenStore(DataLayer& db, uint64_t appVersion) {
  if (!db.isOpen()) {
    return OpenResult::kCorrupt;
  }
  const uint64_t stored = db.schemaVersion();
  if (appVersion < stored) {
    return OpenResult::kVersionMismatch;
  }
  return OpenResult::kOk;
}

}  // namespace muse::net
