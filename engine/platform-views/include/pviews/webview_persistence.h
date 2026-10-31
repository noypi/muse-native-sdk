#pragma once

#include <map>
#include <string>

struct StoreSnapshot {
  std::map<std::string, std::string> cookies;

  bool empty() const { return cookies.empty(); }
};

struct PersistResult {
  bool ok = false;
  std::string error;
};

struct LoadResult {
  bool ok = false;
  StoreSnapshot snap;
  std::string error;
};

// Points the file-backed store at `dir` (hermetic test entry point;
// production passes its app-scoped dir). Stateless: save writes, load
// reads, so a save/load round-trip simulates process death.
void setStoreDir(const std::string& dir);

// Saves `snap`; `clearOnExit=true` wipes the store and reports ok:true
// with an empty snapshot on the next load. Never throws; I/O failure
// returns {false, "IO:<reason>"}.
PersistResult saveStore(const StoreSnapshot& snap, bool clearOnExit) noexcept;

// Restores the store: missing store file in an existing directory
// returns ok:true with an empty snapshot. Never throws; I/O failure
// returns {false, {}, "IO:<reason>"}.
LoadResult loadStore() noexcept;
