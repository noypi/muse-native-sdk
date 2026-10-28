#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/sqlite_store.h"

namespace muse::net {

// A single forward migration step: advances the schema from `from` to `to`
// by executing `stmts` in order inside one backend transaction.
struct Migration {
  uint64_t from = 0;
  uint64_t to = 0;
  std::vector<std::string> stmts;
};

// Outcome of Migrate: kOk on success, kRolledBack when a step (or a chain
// gap) left the version unchanged, kDowngradeRefused when target < current
// and no down-chain exists (store untouched).
enum class MigrateResult {
  kOk,
  kRolledBack,
  kDowngradeRefused,
};

// Applies the forward chain in vector order until `userVersion() == target`:
// - target == current is a no-op success (zero statements executed);
// - target < current returns kDowngradeRefused with the store untouched;
// - each step's statements pass verbatim to ISqlBackend::exec; a failed
//   statement aborts with kRolledBack and the version unchanged;
// - a target unreachable from current through the chain fails closed with
//   kRolledBack and the version unchanged (no partial advance).
MigrateResult Migrate(ISqlBackend& b, const std::vector<Migration>& chain,
                      uint64_t target);

}  // namespace muse::net
