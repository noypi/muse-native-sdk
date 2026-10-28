// Batch 3 contract tests — FR-NET-011 / AC-NET-022 (TC-NET-022).
// Covers T5 (Outbox + ApplyWithOutbox transactional mutation queue).
// Hermetic: C++17 + net headers only; no network, no env/config reads,
// no live services, no disk. SQL behavior is faked behind the T2
// ISqlBackend seam with a programmable exec result (fault injection);
// the outbox itself runs for real through its public API.
// Binds ONLY to the T5 `Interface:` line (plus the T2 ISqlBackend seam it
// names); never to implementation.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "net/outbox.h"
#include "net/sqlite_store.h"

namespace {

// Programmable fake SQL backend behind the ISqlBackend seam. Scripted
// exec result drives fault injection: returning false simulates the row
// write failing so the test can assert the outbox enqueue rolled back.
class FakeBackend : public muse::net::ISqlBackend {
 public:
  explicit FakeBackend(bool execResult = true) : execResult_(execResult) {}
  bool exec(std::string_view sql) override {
    seenSql_.emplace_back(sql);
    if (failOnNext_) {
      failOnNext_ = false;
      return false;
    }
    return execResult_;
  }
  uint64_t userVersion() const override { return version_; }
  void setUserVersion(uint64_t v) override { version_ = v; }
  void setExecResult(bool ok) { execResult_ = ok; }
  void failNextExec() { failOnNext_ = true; }
  const std::vector<std::string>& seenSql() const { return seenSql_; }

 private:
  bool execResult_ = true;
  bool failOnNext_ = false;
  uint64_t version_ = 0;
  std::vector<std::string> seenSql_;
};

muse::net::Mutation makeMutation(uint64_t id, const std::string& table = "notes",
                                 const std::string& payload = "{\"op\":\"upsert\"}") {
  muse::net::Mutation m;
  m.id = id;
  m.table = table;
  m.payload = payload;
  return m;
}

}  // namespace

// Named case: enqueue then pending round-trips the mutation fields.
static bool check_enqueue_pending_round_trip() {
  muse::net::Outbox o;
  if (!o.enqueue(makeMutation(1, "notes", "p1"))) return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  if (p.size() != 1) return false;
  return p[0].id == 1 && p[0].table == "notes" && p[0].payload == "p1";
}

// Named case: commit removes only the acked id; other ids stay pending
// (exactly-once discipline).
static bool check_commit_removes_only_acked_id() {
  muse::net::Outbox o;
  if (!o.enqueue(makeMutation(1))) return false;
  if (!o.enqueue(makeMutation(2))) return false;
  if (!o.enqueue(makeMutation(3))) return false;
  if (!o.commit(2)) return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  if (p.size() != 2) return false;
  bool has1 = false, has3 = false;
  for (const auto& m : p) {
    if (m.id == 1) has1 = true;
    if (m.id == 3) has3 = true;
    if (m.id == 2) return false;
  }
  return has1 && has3;
}

// Named case: committing an unknown id returns false and leaves pending
// untouched.
// ASSUMPTION (A-T5-unknown-commit): commit of an id with no entry returns
// false (the criterion pins removal of the acked id; failing closed on an
// unknown id is the most defensible reading).
static bool check_commit_unknown_id_fails_closed() {
  muse::net::Outbox o;
  if (!o.enqueue(makeMutation(1))) return false;
  if (o.commit(999)) return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  return p.size() == 1 && p[0].id == 1;
}

// Named case: ApplyWithOutbox success writes the row and enqueues in one
// backend transaction — returns true and the mutation is pending.
static bool check_apply_success_enqueues() {
  FakeBackend b(true);
  muse::net::Outbox o;
  if (!muse::net::ApplyWithOutbox(b, o, makeMutation(10, "notes", "hello"),
                                  "INSERT INTO notes VALUES ('hello')"))
    return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  if (p.size() != 1) return false;
  return p[0].id == 10 && p[0].table == "notes" && p[0].payload == "hello";
}

// Named case: fault during the row write leaves zero outbox entries — the
// row write and the enqueue roll back together.
static bool check_apply_sql_failure_leaves_zero_entries() {
  FakeBackend b(true);
  b.failNextExec();  // row-write half fails
  muse::net::Outbox o;
  if (muse::net::ApplyWithOutbox(b, o, makeMutation(11, "notes", "boom"),
                                 "INSERT INTO notes VALUES ('boom')"))
    return false;
  return o.pending().empty();
}

// Named case: a globally failing backend also leaves zero entries.
static bool check_apply_always_failing_backend_leaves_zero_entries() {
  FakeBackend b(false);
  muse::net::Outbox o;
  if (muse::net::ApplyWithOutbox(b, o, makeMutation(12), "INSERT INTO t VALUES (1)"))
    return false;
  return o.pending().empty();
}

// Named case: failed ApplyWithOutbox preserves pre-existing pending entries
// (no partial enqueue, no loss of earlier entries).
static bool check_apply_failure_preserves_existing_pending() {
  FakeBackend b(true);
  muse::net::Outbox o;
  if (!muse::net::ApplyWithOutbox(b, o, makeMutation(20), "INSERT INTO t VALUES (20)"))
    return false;
  b.failNextExec();
  if (muse::net::ApplyWithOutbox(b, o, makeMutation(21), "INSERT INTO t VALUES (21)"))
    return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  return p.size() == 1 && p[0].id == 20;
}

// Named case: commit-then-observe leaves no redeliverable duplicate for the
// acked id on rescan.
static bool check_commit_then_rescan_has_no_duplicate() {
  FakeBackend b(true);
  muse::net::Outbox o;
  if (!muse::net::ApplyWithOutbox(b, o, makeMutation(30), "INSERT INTO t VALUES (30)"))
    return false;
  if (!muse::net::ApplyWithOutbox(b, o, makeMutation(31), "INSERT INTO t VALUES (31)"))
    return false;
  if (!o.commit(30)) return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  if (p.size() != 1) return false;
  return p[0].id == 31;
}

// Named case: binary-opaque payloads round-trip through enqueue/pending.
// ASSUMPTION (A-T5-payload): payload is an opaque byte string (embedded NUL
// preserved); no encoding or size cap is imposed by the T5 interface.
static bool check_enqueue_binary_payload_round_trip() {
  muse::net::Outbox o;
  const std::string blob("a\0b\0c", 5);
  if (!o.enqueue(makeMutation(40, "docs", blob))) return false;
  const std::vector<muse::net::Mutation> p = o.pending();
  if (p.size() != 1) return false;
  return p[0].payload == blob;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_enqueue_pending_round_trip);
  RUN_CASE(check_commit_removes_only_acked_id);
  RUN_CASE(check_commit_unknown_id_fails_closed);
  RUN_CASE(check_apply_success_enqueues);
  RUN_CASE(check_apply_sql_failure_leaves_zero_entries);
  RUN_CASE(check_apply_always_failing_backend_leaves_zero_entries);
  RUN_CASE(check_apply_failure_preserves_existing_pending);
  RUN_CASE(check_commit_then_rescan_has_no_duplicate);
  RUN_CASE(check_enqueue_binary_payload_round_trip);
  if (failures > 0) {
    std::printf("outbox_transaction_test: %d failing\n", failures);
    return 1;
  }
  std::printf("outbox_transaction_test: all passing\n");
  return 0;
}
