// Batch 4 contract tests — FR-NET-011 / AC-NET-024 (TC-NET-024).
// Covers T7 (DurableOutbox replay + FtsSearch seam + failed-migration
// version intact).
// Hermetic: C++17 + net headers only; no network, no env/config reads,
// no live services, no disk. "Kill" is simulated by destroying the fake
// backend mid-mutation and reopening a fresh one while the Outbox (which
// outlives the backend) keeps the unacked entries. FTS corpus is seeded
// through the ISqlBackend exec seam with sentinel INSERT statements; rank
// order is asserted on the observable FtsSearch output only.
// Binds ONLY to the T7 `Interface:` line (plus the T2 ISqlBackend seam and
// the T5 Outbox/Mutation types it names); never to implementation.
// NOTE: reuses `muse::net::Mutation`/`muse::net::Outbox` via `net/outbox.h`
// and `muse::net::ISqlBackend` via `net/sqlite_store.h` — this file must NOT
// redefine them (ODR). See T7.md Conflicts (C-T7-fts-seam-gap): the coder
// must back FtsSearch through the ISqlBackend seam (exec-observed sentinel
// INSERTs and/or a defaulted accessor) so these fakes keep compiling.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "net/durability.h"
#include "net/migration.h"
#include "net/outbox.h"
#include "net/sqlite_store.h"

namespace {

// Programmable fake SQL backend behind the ISqlBackend seam. exec records
// every statement; FTS corpus rows are captured from sentinel INSERTs of the
// form `FTS_SEED <docId> <text>` so the coder's FtsSearch can serve them
// back rank-ordered without this test reaching past the public seam.
class FakeBackend : public muse::net::ISqlBackend {
 public:
  explicit FakeBackend(uint64_t version = 0) : version_(version) {}
  bool exec(std::string_view sql) override {
    seenSql_.emplace_back(sql);
    if (failAll_) return false;
    // Capture the FTS corpus row behind the sentinel INSERT so the coder's
    // FtsSearch can serve it back through the new `query` channel.
    const std::string_view kPrefix = "FTS_SEED ";
    if (sql.size() > kPrefix.size() && sql.substr(0, kPrefix.size()) == kPrefix) {
      const std::string_view rest = sql.substr(kPrefix.size());
      const size_t sp = rest.find(' ');
      const std::string docId =
          std::string(sp == std::string_view::npos ? rest : rest.substr(0, sp));
      const std::string text = std::string(
          sp == std::string_view::npos ? std::string_view{} : rest.substr(sp + 1));
      ftsRows_.emplace_back(docId, text);
    }
    return true;
  }
  // Result-set channel of the T1.10(a) seam extension: serves the corpus rows
  // captured in exec as {docId, text} pairs. Signature matches the defaulted
  // `ISqlBackend::query` exactly so existing fakes keep compiling.
  bool query(std::string_view sql,
             std::vector<std::vector<std::string>>* out) override {
    (void)sql;
    if (out == nullptr) return false;
    if (failAll_) return false;
    out->clear();
    for (const auto& r : ftsRows_) {
      out->push_back({r.first, r.second});
    }
    return true;
  }
  uint64_t userVersion() const override { return version_; }
  void setUserVersion(uint64_t v) override { version_ = v; }
  void setFailAll(bool v) { failAll_ = v; }
  const std::vector<std::string>& seenSql() const { return seenSql_; }

 private:
  uint64_t version_ = 0;
  bool failAll_ = false;
  std::vector<std::string> seenSql_;
  std::vector<std::pair<std::string, std::string>> ftsRows_;
};

muse::net::Mutation makeMutation(uint64_t id, const std::string& table = "notes",
                                 const std::string& payload = "p") {
  muse::net::Mutation m;
  m.id = id;
  m.table = table;
  m.payload = payload;
  return m;
}

// Seeds one FTS document through the public exec seam (sentinel INSERT the
// coder's FtsSearch observes; see C-T7-fts-seam-gap).
void seedFts(muse::net::ISqlBackend& b, std::string_view docId,
             std::string_view text) {
  std::string sql = "FTS_SEED ";
  sql += std::string(docId);
  sql += " ";
  sql += std::string(text);
  b.exec(sql);
}

}  // namespace

// Named case: replayUnacked redelivers every unacked mutation exactly once.
static bool check_replay_redelivers_each_unacked_exactly_once() {
  FakeBackend b;
  muse::net::Outbox o;
  o.enqueue(makeMutation(1, "notes", "a"));
  o.enqueue(makeMutation(2, "notes", "b"));
  muse::net::DurableOutbox d(o);
  std::vector<uint64_t> delivered;
  d.replayUnacked([&](const muse::net::Mutation& m) {
    delivered.push_back(m.id);
    return true;
  });
  if (delivered.size() != 2) return false;
  return delivered[0] == 1 && delivered[1] == 2;
}

// Named case: kill = destroy backend mid-mutation + reopen; unacked entries
// persist (the Outbox outlives the backend) and replay redelivers them.
// ASSUMPTION (A-T7-kill): "kill" is simulated in-process by resetting the
// backend while the Outbox survives; no signals, subprocesses, or disk.
static bool check_kill_mid_mutation_unacked_persist_and_replay() {
  auto b = std::make_unique<FakeBackend>();
  muse::net::Outbox o;
  o.enqueue(makeMutation(7, "notes", "unacked"));
  b.reset();                                  // kill mid-mutation
  auto reopened = std::make_unique<FakeBackend>();  // reopen
  (void)reopened;
  muse::net::DurableOutbox d(o);
  int calls = 0;
  uint64_t seen = 0;
  d.replayUnacked([&](const muse::net::Mutation& m) {
    ++calls;
    seen = m.id;
    return true;
  });
  return calls == 1 && seen == 7;
}

// Named case: a send that reports failure leaves the mutation pending so a
// later replay still delivers it (no silent loss).
// ASSUMPTION (A-T7-ack): replayUnacked treats a `true` send result as acked
// (committed, never redelivered) and `false` as still unacked.
static bool check_failed_send_stays_pending_for_next_replay() {
  FakeBackend b;
  muse::net::Outbox o;
  o.enqueue(makeMutation(3));
  muse::net::DurableOutbox d(o);
  int calls = 0;
  d.replayUnacked([&](const muse::net::Mutation&) {
    ++calls;
    return false;
  });
  if (calls != 1) return false;
  if (o.pending().size() != 1) return false;
  d.replayUnacked([&](const muse::net::Mutation&) {
    ++calls;
    return true;
  });
  return calls == 2 && o.pending().empty();
}

// Named case: acked mutations are never redelivered on a second replay
// (exactly-once across replays).
static bool check_acked_mutation_never_redelivered() {
  FakeBackend b;
  muse::net::Outbox o;
  o.enqueue(makeMutation(4));
  o.enqueue(makeMutation(5));
  muse::net::DurableOutbox d(o);
  d.replayUnacked([](const muse::net::Mutation&) { return true; });
  int second = 0;
  d.replayUnacked([&](const muse::net::Mutation&) {
    ++second;
    return true;
  });
  return second == 0;
}

// Named case: replay over an empty outbox invokes send zero times.
static bool check_replay_empty_outbox_sends_nothing() {
  FakeBackend b;
  muse::net::Outbox o;
  muse::net::DurableOutbox d(o);
  int calls = 0;
  d.replayUnacked([&](const muse::net::Mutation&) {
    ++calls;
    return true;
  });
  (void)b;
  return calls == 0;
}

// Named case: FTS returns rank-ordered hits across scripts (Latin + CJK).
static bool check_fts_rank_ordered_across_scripts() {
  FakeBackend b;
  seedFts(b, "doc-en", "hello world sync notes");
  seedFts(b, "doc-ja", "同期メモ hello");
  const std::vector<muse::net::FtsHit> hits =
      muse::net::FtsSearch(b, "hello", 10);
  if (hits.size() < 2) return false;
  bool hasEn = false, hasJa = false;
  for (const auto& h : hits) {
    if (h.docId == "doc-en") hasEn = true;
    if (h.docId == "doc-ja") hasJa = true;
  }
  if (!hasEn || !hasJa) return false;
  for (size_t i = 1; i < hits.size(); ++i) {
    if (hits[i - 1].rank < hits[i].rank) return false;
  }
  return true;
}

// Named case: FTS honors the result limit.
static bool check_fts_limit_respected() {
  FakeBackend b;
  seedFts(b, "d1", "alpha beta");
  seedFts(b, "d2", "alpha gamma");
  seedFts(b, "d3", "alpha delta");
  const std::vector<muse::net::FtsHit> hits =
      muse::net::FtsSearch(b, "alpha", 2);
  return hits.size() <= 2 && !hits.empty();
}

// Named case: FTS with no match (and empty query) returns empty, no crash.
// ASSUMPTION (A-T7-fts-empty): an empty query matches nothing; a query with
// no corpus match returns an empty vector rather than an error.
static bool check_fts_no_match_and_empty_query_return_empty() {
  FakeBackend b;
  seedFts(b, "d1", "alpha beta");
  if (!muse::net::FtsSearch(b, "zzz-no-such-term", 10).empty()) return false;
  return muse::net::FtsSearch(b, "", 10).empty();
}

// Named case: hostile FTS query text is treated as data — returns empty,
// never throws or crashes the process.
static bool check_fts_hostile_query_treated_as_data() {
  FakeBackend b;
  seedFts(b, "d1", "alpha beta");
  const std::vector<muse::net::FtsHit> hits =
      muse::net::FtsSearch(b, "' OR '1'='1", 10);
  return hits.empty();
}

// Named case (TC-NET-024 failed-migration half): a migration chain whose
// statement fails leaves the prior version intact.
static bool check_failed_migration_leaves_prior_version_intact() {
  FakeBackend b(1);
  b.setFailAll(true);
  const std::vector<muse::net::Migration> chain = {[&] {
    muse::net::Migration m;
    m.from = 1;
    m.to = 2;
    m.stmts = {"ALTER TABLE t ADD COLUMN v TEXT"};
    return m;
  }()};
  const auto r = muse::net::Migrate(b, chain, 2);
  return r == muse::net::MigrateResult::kRolledBack && b.userVersion() == 1;
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
  RUN_CASE(check_replay_redelivers_each_unacked_exactly_once);
  RUN_CASE(check_kill_mid_mutation_unacked_persist_and_replay);
  RUN_CASE(check_failed_send_stays_pending_for_next_replay);
  RUN_CASE(check_acked_mutation_never_redelivered);
  RUN_CASE(check_replay_empty_outbox_sends_nothing);
  RUN_CASE(check_fts_rank_ordered_across_scripts);
  RUN_CASE(check_fts_limit_respected);
  RUN_CASE(check_fts_no_match_and_empty_query_return_empty);
  RUN_CASE(check_fts_hostile_query_treated_as_data);
  RUN_CASE(check_failed_migration_leaves_prior_version_intact);
  if (failures > 0) {
    std::printf("durability_test: %d failing\n", failures);
    return 1;
  }
  std::printf("durability_test: all passing\n");
  return 0;
}
