#pragma once

#include <cstdint>
#include <string_view>

#include "net/data_layer.h"

namespace muse::net {

// Result of opening a SQLite-backed store through the version gate.
enum class OpenResult {
  kOk,
  kCorrupt,
  kVersionMismatch,
};

// Seam behind which all SQLite/WAL/FTS5 behavior lives (faked in tests).
// Declared here; IExecutor/IKeyProvider stay declared in net/data_layer.h
// and are not redefined.
struct ISqlBackend {
  virtual bool exec(std::string_view sql) = 0;
  virtual uint64_t userVersion() const = 0;
  virtual void setUserVersion(uint64_t v) = 0;
  virtual ~ISqlBackend() = default;
};

// Version gate over an already-constructed DataLayer:
// - store never opened (!isOpen()) -> kCorrupt (fail closed);
// - appVersion older than the stored schema version -> kVersionMismatch,
//   store left open and version unchanged (downgrade refused, data untouched);
// - otherwise (matching or newer appVersion, upgrade path owned by T6) -> kOk.
OpenResult OpenStore(DataLayer& db, uint64_t appVersion);

}  // namespace muse::net
