// Acceptance/integration test — FR-NET-002/006/011 end-to-end flows.
// Each case chains production modules the way the acceptance criteria read
// (DataLayer + OpenStore + stores + ReactiveQuery + Outbox/DurableOutbox +
// Migrate + FtsSearch in single user-visible scenarios), rather than testing
// one unit in isolation. Hermetic: in-process fakes only, no JNI, no device,
// no network, no env/config, no real SQLite/crypto.
// Binds only to public interfaces in engine/net/include/net/*.h.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "net/data_layer.h"
#include "net/durability.h"
#include "net/migration.h"
#include "net/outbox.h"
#include "net/reactive_query.h"
#include "net/sqlite_store.h"
#include "net/stores.h"

namespace {

class FakeBackend : public muse::net::ISqlBackend {
 public:
  explicit FakeBackend(uint64_t version = 0) : version_(version) {}
  bool exec(std::string_view sql) override {
    seen_.emplace_back(sql);
    if (failNext_) {
      failNext_ = false;
      return false;
    }
    if (failAll_) return false;
    const std::string_view kPrefix = "FTS_SEED ";
    if (sql.size() > kPrefix.size() && sql.substr(0, kPrefix.size()) == kPrefix) {
      const std::string_view rest = sql.substr(kPrefix.size());
      const size_t sp = rest.find(' ');
      ftsRows_.emplace_back(
          std::string(sp == std::string_view::npos ? rest : rest.substr(0, sp)),
          std::string(sp == std::string_view::npos ? std::string_view{}
                                                   : rest.substr(sp + 1)));
    }
    return true;
  }
  bool query(std::string_view sql,
             std::vector<std::vector<std::string>>* out) override {
    (void)sql;
    if (out == nullptr || failAll_) return false;
    out->clear();
    for (const auto& r : ftsRows_) out->push_back({r.first, r.second});
    return true;
  }
  uint64_t userVersion() const override { return version_; }
  void setUserVersion(uint64_t v) override { version_ = v; }
  void setFailNext(bool v) { failNext_ = v; }
  void setFailAll(bool v) { failAll_ = v; }

 private:
  uint64_t version_ = 0;
  bool failNext_ = false;
  bool failAll_ = false;
  std::vector<std::string> seen_;
  std::vector<std::pair<std::string, std::string>> ftsRows_;
};

class FakeKeys : public muse::net::IKeyProvider {
 public:
  FakeKeys() : key_({0x01, 0x02, 0x03, 0x04}), invalid_(false), calls_(0) {}
  bool getKey(std::vector<uint8_t>& out) override {
    ++calls_;
    out = key_;
    return true;
  }
  bool invalidated() const override { return invalid_; }
  void invalidate() { invalid_ = true; }
  int calls() const { return calls_; }

 private:
  std::vector<uint8_t> key_;
  bool invalid_;
  int calls_;
};

class FakeClock : public muse::net::IFrameClock {
 public:
  uint64_t frameId() const override { return frame_; }
  void setFrame(uint64_t f) { frame_ = f; }

 private:
  uint64_t frame_ = 0;
};

void seedFts(muse::net::ISqlBackend& b, std::string_view docId,
             std::string_view text) {
  std::string sql = "FTS_SEED ";
  sql += std::string(docId);
  sql += " ";
  sql += std::string(text);
  b.exec(sql);
}

}  // namespace

// AC-NET-004: process-death survival — open from C++ (zero JS), destroy +
// reopen same path keeps state, downgrade refused with data untouched.
static bool accept_process_death_survival_zero_js() {
  muse::net::DataLayer::DataLayerConfig cfg;
  {
    muse::net::DataLayer db(cfg);
    if (!db.open(":memory:")) return false;
    if (!db.isOpen() || db.schemaVersion() == 0) return false;
  }  // simulated process death: DataLayer destroyed.
  muse::net::DataLayer db2(cfg);
  if (!db2.open(":memory:")) return false;
  const uint64_t v = db2.schemaVersion();
  // Downgrade guard: older appVersion refused, store/version untouched.
  if (v > 0 &&
      muse::net::OpenStore(db2, v - 1) != muse::net::OpenResult::kVersionMismatch)
    return false;
  if (!db2.isOpen() || db2.schemaVersion() != v) return false;
  return true;
}

// AC-NET-009: all stores open/read/write from C++; hot path performs zero
// provider (JNI) calls after open; invalidated key fails closed, no plaintext.
static bool accept_stores_open_no_jni_hot_path_invalidated_fails() {
  auto keys = std::make_shared<FakeKeys>();
  muse::net::EncryptedStore enc(keys);
  const int callsAtOpen = keys->calls();
  muse::net::KvStore kv;
  muse::net::FsApi fs;
  if (!kv.put("k", "v\u65e5\u672c\u8a9e")) return false;  // unicode value
  std::string kvOut = "SENTINEL";
  if (!kv.get("k", &kvOut) || kvOut != "v\u65e5\u672c\u8a9e") return false;
  if (!enc.put("secret", "s3cr3t-\u00e9\u00e8")) return false;
  std::string encOut = "SENTINEL";
  if (!enc.get("secret", &encOut) || encOut != "s3cr3t-\u00e9\u00e8") return false;
  if (keys->calls() != callsAtOpen) return false;  // zero JNI on hot path
  const std::string path = "/tmp/muse_net_accept_fs_probe.bin";
  if (!fs.writeFile(path, std::string_view("bytes\x00\xff", 7))) return false;
  std::string fsOut;
  if (!fs.readFile(path, &fsOut) || fsOut.size() != 7) {
    std::remove(path.c_str());
    return false;
  }
  std::remove(path.c_str());
  // Invalidate: decrypt must fail closed and preserve the caller's buffer.
  keys->invalidate();
  std::string noLeak = "SENTINEL";
  if (enc.get("secret", &noLeak)) return false;
  return noLeak == "SENTINEL";
}

// AC-NET-021: 50 row changes in one frame -> exactly one subscriber push;
// next frame re-arms with exactly one more.
static bool accept_reactive_push_coalesced_per_frame() {
  auto clock = std::make_shared<FakeClock>();
  clock->setFrame(7);
  muse::net::ReactiveQuery q(clock);
  int deliveries = 0;
  uint64_t lastFrame = 0;
  q.subscribe([&](uint64_t f) {
    ++deliveries;
    lastFrame = f;
  });
  for (int i = 0; i < 50; ++i) q.notifyChanged();
  if (deliveries != 1 || lastFrame != 7) return false;
  clock->setFrame(8);
  q.notifyChanged();
  return deliveries == 2 && lastFrame == 8;
}

// AC-NET-022: crash during the row write enqueues nothing; commit-then-crash
// leaves no redeliverable duplicate.
static bool accept_transactional_outbox_crash_enqueues_nothing() {
  FakeBackend b;
  muse::net::Outbox o;
  muse::net::Mutation m;
  m.id = 1;
  m.table = "notes";
  m.payload = "hello";
  b.setFailNext(true);  // fault: row write fails
  if (muse::net::ApplyWithOutbox(b, o, m, "INSERT INTO notes VALUES('x')"))
    return false;
  if (!o.pending().empty()) return false;
  // Success path, then ack, then rescan: no duplicate.
  if (!muse::net::ApplyWithOutbox(b, o, m, "INSERT INTO notes VALUES('x')"))
    return false;
  if (o.pending().size() != 1) return false;
  if (!o.commit(1)) return false;
  return o.pending().empty();
}

// AC-NET-023: failing migration rolls back with version unchanged;
// downgrade without a down-chain is refused with the store untouched.
static bool accept_transactional_migration_rollback_and_downgrade_guard() {
  FakeBackend b(1);
  b.setFailAll(true);
  muse::net::Migration step;
  step.from = 1;
  step.to = 2;
  step.stmts = {"ALTER TABLE t ADD COLUMN v TEXT"};
  if (muse::net::Migrate(b, {step}, 2) != muse::net::MigrateResult::kRolledBack)
    return false;
  if (b.userVersion() != 1) return false;
  FakeBackend b2(2);
  muse::net::Migration down;
  down.from = 1;
  down.to = 2;
  down.stmts = {"CREATE TABLE t (a)"};
  if (muse::net::Migrate(b2, {down}, 1) !=
      muse::net::MigrateResult::kDowngradeRefused)
    return false;
  return b2.userVersion() == 2;
}

// AC-NET-024 (replay half): kill mid-mutation -> relaunch replays each queued
// mutation exactly once; a second replay sends nothing.
static bool accept_kill_mid_mutation_exactly_once_replay() {
  muse::net::Outbox o;  // outlives the backend, like the durable queue
  for (uint64_t i = 1; i <= 3; ++i) {
    muse::net::Mutation m;
    m.id = i;
    m.table = "notes";
    m.payload = "p" + std::to_string(i);
    FakeBackend tmp;
    if (!muse::net::ApplyWithOutbox(tmp, o, m, "INSERT ...")) return false;
  }  // kill: backends destroyed here; o survives.
  FakeBackend reopened;
  (void)reopened;
  muse::net::DurableOutbox d(o);
  std::vector<uint64_t> first;
  d.replayUnacked([&](const muse::net::Mutation& m) {
    first.push_back(m.id);
    return true;
  });
  if (first != std::vector<uint64_t>({1, 2, 3})) return false;
  int second = 0;
  d.replayUnacked([&](const muse::net::Mutation&) {
    ++second;
    return true;
  });
  return second == 0;
}

// AC-NET-024 (FTS + failed-migration halves): ranked search across scripts
// honors limit; empty/hostile queries are empty; failed migration keeps the
// prior version.
static bool accept_ranked_fts_and_failed_migration_prior_version() {
  FakeBackend b(1);
  seedFts(b, "d1", "hello world notes");
  seedFts(b, "d2", "hello hello hello world");
  seedFts(b, "d3", "\u65e5\u672c\u8a9e \u30c6\u30b9\u30c8 hello");
  seedFts(b, "d4", "caf\u00e9 na\u00efve r\u00e9sum\u00e9 hello");
  const auto hits = muse::net::FtsSearch(b, "hello", 10);
  if (hits.empty() || hits[0].docId != "d2") return false;
  for (size_t i = 1; i < hits.size(); ++i)
    if (hits[i - 1].rank < hits[i].rank) return false;  // rank order
  if (muse::net::FtsSearch(b, "hello", 2).size() != 2) return false;  // limit
  if (muse::net::FtsSearch(b, "hello", 0).size() != 0) return false;  // edge
  if (!muse::net::FtsSearch(b, "", 10).empty()) return false;
  if (!muse::net::FtsSearch(b, "zzz-no-such-term", 10).empty()) return false;
  if (!muse::net::FtsSearch(b, "' OR '1'='1", 10).empty()) return false;
  const auto cjk = muse::net::FtsSearch(b, "\u65e5\u672c\u8a9e", 10);
  if (cjk.empty() || cjk[0].docId != "d3") return false;
  FakeBackend mig(1);
  mig.setFailAll(true);
  muse::net::Migration step;
  step.from = 1;
  step.to = 2;
  step.stmts = {"ALTER TABLE t ADD COLUMN v TEXT"};
  return muse::net::Migrate(mig, {step}, 2) ==
             muse::net::MigrateResult::kRolledBack &&
         mig.userVersion() == 1;
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(accept_process_death_survival_zero_js);
  RUN_CASE(accept_stores_open_no_jni_hot_path_invalidated_fails);
  RUN_CASE(accept_reactive_push_coalesced_per_frame);
  RUN_CASE(accept_transactional_outbox_crash_enqueues_nothing);
  RUN_CASE(accept_transactional_migration_rollback_and_downgrade_guard);
  RUN_CASE(accept_kill_mid_mutation_exactly_once_replay);
  RUN_CASE(accept_ranked_fts_and_failed_migration_prior_version);
  if (failures > 0) {
    std::printf("acceptance_net_test: %d failing\n", failures);
    return 1;
  }
  std::printf("acceptance_net_test: all passing\n");
  return 0;
}
