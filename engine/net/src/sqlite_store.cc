#include "net/sqlite_store.h"

// Production note (T1.10 option (a)): real FTS5 rows are served through the
// `ISqlBackend::query` override on the concrete SQLite backend (fixed SQL,
// no query-text interpolation); this slice links no real SQLite (seams only,
// see A2), so the default (`false`, fail closed) applies here and FtsSearch
// yields empty when no rows are observable.

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
