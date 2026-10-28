#include "net/migration.h"

namespace muse::net {

MigrateResult Migrate(ISqlBackend& b, const std::vector<Migration>& chain,
                      uint64_t target) {
  const uint64_t current = b.userVersion();
  if (target == current) {
    return MigrateResult::kOk;
  }
  if (target < current) {
    return MigrateResult::kDowngradeRefused;
  }
  // Plan the steps without touching the backend so a gap beyond the chain
  // tip (or a broken link) fails closed with zero statements executed.
  std::vector<const Migration*> plan;
  uint64_t cursor = current;
  for (const auto& m : chain) {
    if (cursor == target) {
      break;
    }
    if (m.to <= current) {
      continue;
    }
    if (m.from != cursor) {
      // Not the next link: skip steps that end at or before the cursor,
      // otherwise the chain cannot reach the target from here.
      if (m.to <= cursor) {
        continue;
      }
      return MigrateResult::kRolledBack;
    }
    if (m.to <= m.from || m.to > target) {
      return MigrateResult::kRolledBack;
    }
    plan.push_back(&m);
    cursor = m.to;
  }
  if (cursor != target) {
    return MigrateResult::kRolledBack;
  }
  // Execute the plan; version writes are deferred until every statement
  // succeeds so a mid-chain failure leaves the version at its prior value.
  for (const Migration* m : plan) {
    for (const auto& stmt : m->stmts) {
      if (!b.exec(stmt)) {
        return MigrateResult::kRolledBack;
      }
    }
  }
  b.setUserVersion(target);
  return MigrateResult::kOk;
}

}  // namespace muse::net
