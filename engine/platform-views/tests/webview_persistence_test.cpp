// T2 contract tests — FR-PVIEWS-011 / AC-PVIEWS-017.
// Binds ONLY to the plan T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/webview_persistence.h only; temp-dir store under the
// OS temp area with a unique subdir per test (created and removed in-test);
// process death simulated by destroy/recreate round-trip (save then load, no
// in-memory carry-over); no SDK downloads, no real credentials/tokens, no
// env/config reads, no live services (GR-05 R5.3).
// ASSUMPTION (see unit-tests/T2.md): the header additionally declares
//   void setStoreDir(const std::string& dir);
// StoreSnapshot is default-constructible with a map-like `cookies` member and
// an `empty()` predicate; PersistResult is `{ bool ok; std::string error; }`.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "pviews/webview_persistence.h"

namespace {

namespace fs = std::filesystem;

int uniqueCounter = 0;

std::string makeTempDir() {
  fs::path dir = fs::temp_directory_path() /
                 ("pviews_store_test_" + std::to_string(++uniqueCounter));
  fs::create_directories(dir);
  return dir.string();
}

void removeTempDir(const std::string& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
}

StoreSnapshot makeSnap() {
  StoreSnapshot snap{};
  snap.cookies["session"] = "dummy-cookie-not-real";
  snap.cookies["prefs"] = "dummy-dom-value-not-real";
  return snap;
}

// Case: cookies survive a destroy/recreate round-trip (save then load).
bool check_round_trip_survives() {
  const std::string dir = makeTempDir();
  setStoreDir(dir);
  const PersistResult pr = saveStore(makeSnap(), false);
  const LoadResult lr = loadStore();
  removeTempDir(dir);
  return pr.ok && lr.ok && lr.error.empty() &&
         lr.snap.cookies == makeSnap().cookies;
}

// Case: clearOnExit=true forces an empty snapshot with ok:true.
bool check_clear_on_exit_forces_empty() {
  const std::string dir = makeTempDir();
  setStoreDir(dir);
  saveStore(makeSnap(), false);
  const PersistResult pr = saveStore(makeSnap(), true);
  const LoadResult lr = loadStore();
  removeTempDir(dir);
  return pr.ok && lr.ok && lr.snap.empty();
}

// Case: clearOnExit on an empty store is still ok:true with an empty snapshot.
bool check_clear_on_exit_empty_store_ok() {
  const std::string dir = makeTempDir();
  setStoreDir(dir);
  const PersistResult pr = saveStore(StoreSnapshot{}, true);
  const LoadResult lr = loadStore();
  removeTempDir(dir);
  return pr.ok && lr.ok && lr.snap.empty();
}

// Case: I/O failure (store path is a regular file, not a dir) returns
// ok:false with an "IO:<reason>" error, never throws.
bool check_io_failure_shape() {
  const std::string dir = makeTempDir();
  const std::string blocker = dir + "/blocker";
  {
    std::ofstream out(blocker);
    out << "not-a-directory";
  }
  setStoreDir(blocker);
  bool threw = false;
  LoadResult lr{};
  try {
    saveStore(makeSnap(), false);
    lr = loadStore();
  } catch (...) {
    threw = true;
  }
  removeTempDir(dir);
  return !threw && !lr.ok && lr.error.rfind("IO:", 0) == 0;
}

// Case: fresh directory load never throws and returns a well-formed result:
// either ok with an empty snapshot, or ok:false with an "IO:" error.
bool check_fresh_dir_load_well_formed() {
  const std::string dir = makeTempDir();
  setStoreDir(dir);
  bool threw = false;
  LoadResult lr{};
  try {
    lr = loadStore();
  } catch (...) {
    threw = true;
  }
  removeTempDir(dir);
  if (threw) return false;
  if (lr.ok) return lr.snap.empty();
  return lr.error.rfind("IO:", 0) == 0;
}

// Case: a large cookie jar round-trips intact (boundary: bulk DOM storage).
bool check_large_store_round_trip() {
  const std::string dir = makeTempDir();
  setStoreDir(dir);
  StoreSnapshot snap{};
  for (int i = 0; i < 500; ++i) {
    snap.cookies["k" + std::to_string(i)] =
        "v" + std::string(200, static_cast<char>('a' + (i % 26)));
  }
  const PersistResult pr = saveStore(snap, false);
  const LoadResult lr = loadStore();
  removeTempDir(dir);
  return pr.ok && lr.ok && lr.snap.cookies == snap.cookies;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_round_trip_survives);
  RUN_CASE(check_clear_on_exit_forces_empty);
  RUN_CASE(check_clear_on_exit_empty_store_ok);
  RUN_CASE(check_io_failure_shape);
  RUN_CASE(check_fresh_dir_load_well_formed);
  RUN_CASE(check_large_store_round_trip);
  if (failures > 0) {
    std::printf("webview_persistence_test: %d failing\n", failures);
    return 1;
  }
  std::printf("webview_persistence_test: all passing\n");
  return 0;
}
