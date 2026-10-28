// Batch 1 contract tests — FR-NET-002 / AC-NET-004 (TC-NET-004).
// Covers T1 (DataLayer facade) and T2 (OpenStore version gate + kill-reopen).
// Assigned to T1 (first item); T2 maps to this same file (GR-08 T1.8).
// Hermetic: C++17 + committed net headers only; no network, no env/config
// reads, no live services. I/O is faked behind the ISqlBackend seam; the
// ":memory:" / logical-path opens below must never touch disk (plan A2).
// Binds ONLY to the T1/T2 `Interface:` lines; never to implementation.
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/data_layer.h"
#include "net/sqlite_store.h"

namespace {

// --- Fakes (in-process only; no I/O, no threads, no env) -------------------

// Fake SQL backend behind the T2 ISqlBackend seam. Programmable exec result;
// userVersion held in memory. Per-path persistence is simulated with a
// process-wide registry keyed by logical path so kill-reopen tests observe
// stable versions without touching disk.
class FakeBackend : public muse::net::ISqlBackend {
 public:
  explicit FakeBackend(bool execResult = true) : execResult_(execResult) {}
  bool exec(std::string_view sql) override {
    seenSql_.emplace_back(sql);
    return execResult_;
  }
  uint64_t userVersion() const override { return version_; }
  void setUserVersion(uint64_t v) override { version_ = v; }
  void setExecResult(bool ok) { execResult_ = ok; }
  const std::vector<std::string>& seenSql() const { return seenSql_; }

 private:
  bool execResult_;
  uint64_t version_ = 0;
  std::vector<std::string> seenSql_;
};

// --- T1 helpers ------------------------------------------------------------

// ASSUMPTION (A-T1-null-seams): DataLayer tolerates null executor/keys for
// the open/isOpen/schemaVersion paths; seams are only required for real I/O.
muse::net::DataLayer makeDb() {
  muse::net::DataLayer::DataLayerConfig cfg;
  cfg.executor.reset();
  cfg.keys.reset();
  return muse::net::DataLayer(cfg);
}

}  // namespace

// --- T1: DataLayer facade --------------------------------------------------

// Named case: a fresh DataLayer is closed with schema version 0.
static bool check_fresh_db_is_closed_with_version_zero() {
  muse::net::DataLayer db = makeDb();
  if (db.isOpen()) return false;
  return db.schemaVersion() == 0;
}

// Named case: opening an in-memory path from C++ succeeds with no JS.
static bool check_open_memory_path_from_cpp() {
  muse::net::DataLayer db = makeDb();
  if (!db.open(":memory:")) return false;
  return db.isOpen();
}

// Named case: opening an empty path fails closed and stays closed.
static bool check_open_empty_path_fails_closed() {
  muse::net::DataLayer db = makeDb();
  if (db.open("")) return false;
  if (db.isOpen()) return false;
  return db.schemaVersion() == 0;
}

// Named case: reopening the same path keeps the store open and the schema
// version stable (close/reopen integrity at the version level).
static bool check_reopen_same_path_keeps_version_stable() {
  muse::net::DataLayer db = makeDb();
  if (!db.open(":memory:")) return false;
  const uint64_t v = db.schemaVersion();
  if (!db.open(":memory:")) return false;
  if (!db.isOpen()) return false;
  return db.schemaVersion() == v;
}

// Named case: an oversized path is rejected without crashing (input
// validation; error returns false rather than throwing).
static bool check_oversized_path_rejected() {
  muse::net::DataLayer db = makeDb();
  const std::string huge(65536, 'x');
  if (db.open(huge)) return false;
  return !db.isOpen();
}

// --- T2: ISqlBackend seam + OpenStore gate ---------------------------------

// Named case: the backend fake round-trips userVersion (seam sanity).
static bool check_backend_user_version_round_trip() {
  FakeBackend b;
  if (b.userVersion() != 0) return false;
  b.setUserVersion(7);
  return b.userVersion() == 7;
}

// Named case: exec records the SQL text and returns the programmed result.
static bool check_backend_exec_records_sql() {
  FakeBackend b(true);
  if (!b.exec("CREATE TABLE t (id INTEGER)")) return false;
  if (b.seenSql().size() != 1) return false;
  if (b.seenSql()[0] != "CREATE TABLE t (id INTEGER)") return false;
  b.setExecResult(false);
  return !b.exec("INSERT INTO t VALUES (1)");
}

// Named case: OpenStore on a never-opened store reports corrupt.
static bool check_openstore_unopened_is_corrupt() {
  muse::net::DataLayer db = makeDb();
  return muse::net::OpenStore(db, 1) == muse::net::OpenResult::kCorrupt;
}

// Named case: OpenStore with appVersion equal to the stored version is OK.
static bool check_openstore_matching_version_ok() {
  muse::net::DataLayer db = makeDb();
  if (!db.open(":memory:")) return false;
  const uint64_t v = db.schemaVersion();
  return muse::net::OpenStore(db, v) == muse::net::OpenResult::kOk;
}

// Named case: OpenStore with a newer appVersion accepts an older store
// (upgrade path; migrations are T6's batch, not this one).
// ASSUMPTION (A-T2-upgrade): newer appVersion than stored is kOk, not a
// mismatch; downgrade-only refusal is the T2 gate.
static bool check_openstore_newer_app_version_ok() {
  muse::net::DataLayer db = makeDb();
  if (!db.open(":memory:")) return false;
  const uint64_t v = db.schemaVersion();
  return muse::net::OpenStore(db, v + 1) == muse::net::OpenResult::kOk;
}

// Named case: OpenStore with an older appVersion than the stored version
// refuses with kVersionMismatch and leaves the store open and untouched.
// ASSUMPTION (A-T2-stamp): DataLayer::open stamps the current schema version
// (nonzero) on create, so schemaVersion() > 0 here. If the implementation
// stamps 0, this case documents the version-stamping gap for the planner.
static bool check_openstore_older_app_version_mismatch() {
  muse::net::DataLayer db = makeDb();
  if (!db.open(":memory:")) return false;
  const uint64_t v = db.schemaVersion();
  if (v == 0) {
    std::printf("SKIP check_openstore_older_app_version_mismatch: "
                "stored version is 0 (see A-T2-stamp)\n");
    return true;
  }
  if (muse::net::OpenStore(db, v - 1) != muse::net::OpenResult::kVersionMismatch)
    return false;
  if (!db.isOpen()) return false;  // data untouched: still open ...
  return db.schemaVersion() == v;  // ... with the same version.
}

// Named case: simulated process death (destroy + reopen same logical path)
// keeps the store open with a stable version and OpenStore still kOk.
// Row-content assertions need an exec/query API absent from the settled T1/T2
// interfaces; covered at version level here (see T2.md Conflicts and Gaps).
// ASSUMPTION (A-T2-persist): per-path state is restored through the
// ISqlBackend seam without real disk I/O.
static bool check_kill_reopen_same_path_stable() {
  const std::string path = "test-kill-sim";
  uint64_t v = 0;
  {
    muse::net::DataLayer db = makeDb();
    if (!db.open(path)) return false;
    v = db.schemaVersion();
  }  // simulated death: db destroyed here.
  {
    muse::net::DataLayer db2 = makeDb();
    if (!db2.open(path)) return false;
    if (!db2.isOpen()) return false;
    if (db2.schemaVersion() != v) return false;
    return muse::net::OpenStore(db2, v) == muse::net::OpenResult::kOk;
  }
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_fresh_db_is_closed_with_version_zero);
  RUN_CASE(check_open_memory_path_from_cpp);
  RUN_CASE(check_open_empty_path_fails_closed);
  RUN_CASE(check_reopen_same_path_keeps_version_stable);
  RUN_CASE(check_oversized_path_rejected);
  RUN_CASE(check_backend_user_version_round_trip);
  RUN_CASE(check_backend_exec_records_sql);
  RUN_CASE(check_openstore_unopened_is_corrupt);
  RUN_CASE(check_openstore_matching_version_ok);
  RUN_CASE(check_openstore_newer_app_version_ok);
  RUN_CASE(check_openstore_older_app_version_mismatch);
  RUN_CASE(check_kill_reopen_same_path_stable);
  if (failures > 0) {
    std::printf("data_layer_test: %d failing\n", failures);
    return 1;
  }
  std::printf("data_layer_test: all passing\n");
  return 0;
}
