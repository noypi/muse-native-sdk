// Batch 4 contract tests — FR-NET-011 / AC-NET-023 (TC-NET-023).
// Covers T6 (versioned migration chain with per-step transactions).
// Hermetic: C++17 + net headers only; no network, no env/config reads,
// no live services, no disk. SQL behavior is faked behind the landed T2
// ISqlBackend seam with scripted exec failures (fault injection); version
// bookkeeping is observed through userVersion()/setUserVersion().
// Binds ONLY to the T6 `Interface:` line (plus the T2 ISqlBackend seam it
// names); never to implementation.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "net/migration.h"
#include "net/sqlite_store.h"

namespace {

// Programmable fake SQL backend behind the ISqlBackend seam. exec consults
// a scripted failure list: any statement containing a registered needle
// fails, everything else succeeds. Failure injection simulates a statement
// failing mid-chain so the test can assert rollback semantics.
class FakeBackend : public muse::net::ISqlBackend {
 public:
  explicit FakeBackend(uint64_t version = 0) : version_(version) {}
  bool exec(std::string_view sql) override {
    seenSql_.emplace_back(sql);
    for (const auto& needle : failNeedles_) {
      if (std::string(sql).find(needle) != std::string::npos) return false;
    }
    return true;
  }
  uint64_t userVersion() const override { return version_; }
  void setUserVersion(uint64_t v) override { version_ = v; }
  void failOn(const std::string& needle) { failNeedles_.push_back(needle); }
  const std::vector<std::string>& seenSql() const { return seenSql_; }

 private:
  uint64_t version_ = 0;
  std::vector<std::string> failNeedles_;
  std::vector<std::string> seenSql_;
};

muse::net::Migration step(uint64_t from, uint64_t to, std::vector<std::string> stmts) {
  muse::net::Migration m;
  m.from = from;
  m.to = to;
  m.stmts = std::move(stmts);
  return m;
}

std::vector<muse::net::Migration> twoStepChain() {
  return {
      step(0, 1, {"CREATE TABLE t (id INTEGER)"}),
      step(1, 2, {"ALTER TABLE t ADD COLUMN v TEXT"}),
  };
}

}  // namespace

// Named case: full upgrade applies every step, lands on target, kOk.
static bool check_upgrade_full_chain_ok() {
  FakeBackend b(0);
  const auto r = muse::net::Migrate(b, twoStepChain(), 2);
  return r == muse::net::MigrateResult::kOk && b.userVersion() == 2 &&
         b.seenSql().size() == 2;
}

// Named case: already-at-target is a no-op kOk (no statements executed).
// ASSUMPTION (A-T6-noop): Migrate with target == current version succeeds
// without touching the backend.
static bool check_already_at_target_noop_ok() {
  FakeBackend b(2);
  const auto r = muse::net::Migrate(b, twoStepChain(), 2);
  return r == muse::net::MigrateResult::kOk && b.userVersion() == 2 &&
         b.seenSql().empty();
}

// Named case: failing statement rolls back its step — kRolledBack and the
// version is unchanged (per-step transaction).
static bool check_failing_statement_rolls_back_version_unchanged() {
  FakeBackend b(0);
  b.failOn("ADD COLUMN");
  const auto r = muse::net::Migrate(b, twoStepChain(), 2);
  return r == muse::net::MigrateResult::kRolledBack && b.userVersion() == 0;
}

// Named case: failure on the first step leaves version at its prior value
// (nothing partially applied).
static bool check_first_step_failure_keeps_prior_version() {
  FakeBackend b(0);
  b.failOn("CREATE TABLE");
  const auto r = muse::net::Migrate(b, twoStepChain(), 1);
  return r == muse::net::MigrateResult::kRolledBack && b.userVersion() == 0;
}

// Named case: downgrade request without a down-chain is refused —
// kDowngradeRefused and the store is untouched (version + zero statements).
static bool check_downgrade_without_down_chain_refused_untouched() {
  FakeBackend b(2);
  const auto r = muse::net::Migrate(b, twoStepChain(), 1);
  return r == muse::net::MigrateResult::kDowngradeRefused &&
         b.userVersion() == 2 && b.seenSql().empty();
}

// Named case: partial target upgrades only to the requested version.
static bool check_partial_target_stops_at_target() {
  FakeBackend b(0);
  const auto r = muse::net::Migrate(b, twoStepChain(), 1);
  return r == muse::net::MigrateResult::kOk && b.userVersion() == 1 &&
         b.seenSql().size() == 1;
}

// Named case: empty chain with target == current is kOk (boundary).
// ASSUMPTION (A-T6-empty): an empty chain cannot advance the version; asking
// for the current version is a no-op success.
static bool check_empty_chain_at_current_ok() {
  FakeBackend b(0);
  const auto r = muse::net::Migrate(b, {}, 0);
  return r == muse::net::MigrateResult::kOk && b.userVersion() == 0;
}

// Named case: target beyond the chain tip cannot be satisfied — the version
// must not move past what the chain provides.
// ASSUMPTION (A-T6-gap): a gap between the chain tip and the target fails
// closed with kRolledBack and the version unchanged (no partial advance).
static bool check_target_beyond_chain_tip_fails_closed() {
  FakeBackend b(0);
  const auto r = muse::net::Migrate(b, twoStepChain(), 9);
  if (r == muse::net::MigrateResult::kOk) return false;
  return b.userVersion() == 0;
}

// Named case: statements with hostile text run through the seam as data —
// the migration still applies in order and lands on target (no stripping).
// ASSUMPTION (A-T6-sql-passthrough): Migrate passes statements verbatim to
// exec; quoting/escaping is the backend's job, not the migrator's.
static bool check_hostile_statement_text_passes_through_verbatim() {
  FakeBackend b(0);
  const auto r = muse::net::Migrate(
      b, {step(0, 1, {"INSERT INTO t VALUES ('x''; DROP TABLE t;--')"})}, 1);
  if (r != muse::net::MigrateResult::kOk) return false;
  if (b.userVersion() != 1 || b.seenSql().size() != 1) return false;
  return b.seenSql()[0] == "INSERT INTO t VALUES ('x''; DROP TABLE t;--')";
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_upgrade_full_chain_ok);
  RUN_CASE(check_already_at_target_noop_ok);
  RUN_CASE(check_failing_statement_rolls_back_version_unchanged);
  RUN_CASE(check_first_step_failure_keeps_prior_version);
  RUN_CASE(check_downgrade_without_down_chain_refused_untouched);
  RUN_CASE(check_partial_target_stops_at_target);
  RUN_CASE(check_empty_chain_at_current_ok);
  RUN_CASE(check_target_beyond_chain_tip_fails_closed);
  RUN_CASE(check_hostile_statement_text_passes_through_verbatim);
  if (failures > 0) {
    std::printf("migration_test: %d failing\n", failures);
    return 1;
  }
  std::printf("migration_test: all passing\n");
  return 0;
}
