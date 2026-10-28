// Batch 3 contract tests — FR-NET-003 / AC-NET-006 (TC-NET-006) and
// FR-NET-012 / AC-NET-025 (TC-NET-025).
// Covers T4 (storage-model API surface: all five routes read/write/share via
// injected IStorageBackend fake, no JNI) and T5 (permission-minimal invariant:
// no MANAGE_EXTERNAL_STORAGE; persisted grants survive simulated restart via
// injected IGrantStore fake).
// Hermetic: C++17 + committed net headers only; no network, no env/config
// reads, no live services, no real Android framework. All collaborators are
// in-process fakes; the only persistence is the in-memory FakeGrantStore.
// Binds ONLY to the T4/T5 `Interface:` lines; never to implementation.
// Shared file per GR-08 T1.8: assigned to its first item T4; T5 maps to it.
#include <cstdio>
#include <string>
#include <vector>

#include "net/storage_model.h"
#include "net/storage_permissions.h"

namespace {

// --- Fakes (in-process only; no I/O, no threads, no env) ---------------------

class FakeBackend : public muse::net::IStorageBackend {
 public:
  explicit FakeBackend(muse::net::StorageRoute route) : route_(route) {}

  muse::net::DocRef open(const std::string& scope) override {
    lastScope = scope;
    muse::net::DocRef ref;
    ref.route = route_;
    ref.uri = "uri:" + scope;
    return ref;
  }

  std::string read(const muse::net::DocRef& ref) override {
    ++readCalls;
    for (const auto& entry : store_) {
      if (entry.first == ref.uri) return entry.second;
    }
    return {};
  }

  void write(const muse::net::DocRef& ref, const std::string& data) override {
    ++writeCalls;
    for (auto& entry : store_) {
      if (entry.first == ref.uri) {
        entry.second = data;
        return;
      }
    }
    store_.push_back({ref.uri, data});
  }

  muse::net::DocRef share(const muse::net::DocRef& ref) override {
    ++shareCalls;
    muse::net::DocRef out;
    out.route = muse::net::StorageRoute::kFileProvider;
    out.uri = "content://shared/" + ref.uri;
    return out;
  }

  std::string lastScope;
  int readCalls = 0;
  int writeCalls = 0;
  int shareCalls = 0;

 private:
  muse::net::StorageRoute route_;
  std::vector<std::pair<std::string, std::string>> store_;
};

class FakeGrantStore : public muse::net::IGrantStore {
 public:
  void persist(const muse::net::Grant& grant) override {
    for (auto& entry : grants_) {
      if (entry.uri == grant.uri) {
        entry = grant;
        return;
      }
    }
    grants_.push_back(grant);
  }

  bool has(const std::string& uri) const override {
    for (const auto& entry : grants_) {
      if (entry.uri == uri) return true;
    }
    return false;
  }

  std::vector<muse::net::Grant> restore() const override { return grants_; }

 private:
  std::vector<muse::net::Grant> grants_;
};

// --- T4: all five routes read/write/share ------------------------------------

static bool roundTrip(FakeBackend& backend, const std::string& scope) {
  muse::net::DocRef ref;
  try {
    ref = backend.open(scope);
  } catch (...) {
    return false;
  }
  if (ref.uri.empty()) return false;
  try {
    backend.write(ref, "payload");
  } catch (...) {
    return false;
  }
  std::string out;
  try {
    out = backend.read(ref);
  } catch (...) {
    return false;
  }
  if (out != "payload") return false;
  muse::net::DocRef shared;
  try {
    shared = backend.share(ref);
  } catch (...) {
    return false;
  }
  return shared.route == muse::net::StorageRoute::kFileProvider &&
         !shared.uri.empty();
}

static bool check_app_specific_round_trip() {
  FakeBackend backend(muse::net::StorageRoute::kAppSpecific);
  return roundTrip(backend, "app-scope");
}

static bool check_mediastore_round_trip() {
  FakeBackend backend(muse::net::StorageRoute::kMediaStore);
  return roundTrip(backend, "media-scope");
}

static bool check_saf_round_trip() {
  FakeBackend backend(muse::net::StorageRoute::kSaf);
  return roundTrip(backend, "docs-scope");
}

static bool check_photo_picker_round_trip() {
  FakeBackend backend(muse::net::StorageRoute::kPhotoPicker);
  return roundTrip(backend, "picker-scope");
}

static bool check_fileprovider_round_trip() {
  FakeBackend backend(muse::net::StorageRoute::kFileProvider);
  return roundTrip(backend, "outbound-scope");
}

// Named case: open preserves the backend route on the returned DocRef.
static bool check_open_preserves_route() {
  FakeBackend backend(muse::net::StorageRoute::kSaf);
  const muse::net::DocRef ref = backend.open("scope-a");
  return ref.route == muse::net::StorageRoute::kSaf;
}

// Named case: read of an unknown uri yields empty content, never throws.
static bool check_read_missing_yields_empty_never_throws() {
  FakeBackend backend(muse::net::StorageRoute::kAppSpecific);
  muse::net::DocRef ref;
  ref.route = muse::net::StorageRoute::kAppSpecific;
  ref.uri = "uri:no-such-doc";
  try {
    return backend.read(ref).empty();
  } catch (...) {
    return false;
  }
}

// Named case: write overwrites existing content.
static bool check_write_overwrites() {
  FakeBackend backend(muse::net::StorageRoute::kMediaStore);
  const muse::net::DocRef ref = backend.open("scope-b");
  backend.write(ref, "one");
  backend.write(ref, "two");
  return backend.read(ref) == "two";
}

// Named case: share routes outbound through FileProvider with a content URI.
static bool check_share_uses_fileprovider_content_uri() {
  FakeBackend backend(muse::net::StorageRoute::kSaf);
  const muse::net::DocRef ref = backend.open("scope-c");
  const muse::net::DocRef shared = backend.share(ref);
  if (shared.route != muse::net::StorageRoute::kFileProvider) return false;
  return shared.uri.compare(0, 10, "content://") == 0;
}

// --- T5: permission model ----------------------------------------------------

// Named case: no route requires MANAGE_EXTERNAL_STORAGE.
static bool check_no_route_requires_manage_external_storage() {
  const muse::net::StorageRoute routes[] = {
      muse::net::StorageRoute::kAppSpecific,
      muse::net::StorageRoute::kMediaStore,
      muse::net::StorageRoute::kSaf,
      muse::net::StorageRoute::kPhotoPicker,
      muse::net::StorageRoute::kFileProvider,
  };
  for (const auto route : routes) {
    FakeBackend backend(route);
    const muse::net::DocRef ref = backend.open("perm-scope");
    muse::net::Grant grant;
    grant.uri = ref.uri;
    grant.persistable = true;
    try {
      if (muse::net::RequiresManageExternalStorage(grant)) return false;
    } catch (...) {
      return false;
    }
  }
  return true;
}

// Helper: access check against a store snapshot (has() == accessible).
static bool restored_access(const FakeGrantStore& store,
                            const std::string& uri) {
  return store.has(uri);
}

// Named case: a persistable grant survives simulated restart (new accessor
// over the same store still grants access).
static bool check_persisted_grant_survives_restart() {
  FakeGrantStore store;
  muse::net::Grant grant;
  grant.uri = "uri:docs-scope";
  grant.persistable = true;
  store.persist(grant);
  // Simulated restart: fresh accessor view over the same store contents.
  const FakeGrantStore restarted = store;
  if (!restored_access(restarted, grant.uri)) return false;
  return muse::net::CanAccessAfterRestart(restarted, grant.uri);
}

// Named case: a non-persisted grant is not accessible after restart.
static bool check_non_persisted_grant_lost_after_restart() {
  FakeGrantStore live;
  // Never persisted: the restarted store has no record of this uri.
  const FakeGrantStore restarted;
  (void)live;
  if (restarted.has("uri:transient-scope")) return false;
  return !muse::net::CanAccessAfterRestart(restarted, "uri:transient-scope");
}

// Named case: restore() round-trips every persisted grant.
static bool check_restore_round_trips_persisted_grants() {
  FakeGrantStore store;
  muse::net::Grant photo;
  photo.uri = "uri:picker-scope";
  photo.persistable = true;
  muse::net::Grant docs;
  docs.uri = "uri:docs-scope";
  docs.persistable = true;
  store.persist(photo);
  store.persist(docs);
  const std::vector<muse::net::Grant> restored = store.restore();
  if (restored.size() != 2) return false;
  return store.has(photo.uri) && store.has(docs.uri);
}

// Named case: has() is false for an unknown uri (fail-closed).
static bool check_has_unknown_uri_is_false() {
  FakeGrantStore store;
  return !store.has("uri:no-such-grant");
}

}  // namespace

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
  RUN_CASE(check_app_specific_round_trip);
  RUN_CASE(check_mediastore_round_trip);
  RUN_CASE(check_saf_round_trip);
  RUN_CASE(check_photo_picker_round_trip);
  RUN_CASE(check_fileprovider_round_trip);
  RUN_CASE(check_open_preserves_route);
  RUN_CASE(check_read_missing_yields_empty_never_throws);
  RUN_CASE(check_write_overwrites);
  RUN_CASE(check_share_uses_fileprovider_content_uri);
  RUN_CASE(check_no_route_requires_manage_external_storage);
  RUN_CASE(check_persisted_grant_survives_restart);
  RUN_CASE(check_non_persisted_grant_lost_after_restart);
  RUN_CASE(check_restore_round_trips_persisted_grants);
  RUN_CASE(check_has_unknown_uri_is_false);
  if (failures > 0) {
    std::printf("storage_model_test: %d failing\n", failures);
    return 1;
  }
  std::printf("storage_model_test: all passing\n");
  return 0;
}
