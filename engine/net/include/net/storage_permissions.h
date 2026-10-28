#pragma once

#include <string>
#include <vector>

namespace muse::net {

// Persisted URI grant: a user-selected document URI plus whether it may be
// persisted across restarts. Non-persisted grants have no representation in
// the store (absence from the store == denied after restart).
struct Grant {
  std::string uri;
  bool persistable = false;
};

// Persisted-grant store surface. Implementations persist grants (e.g. SAF
// takePersistableUriPermission) and answer whether a URI is accessible.
// All operations are non-throwing; unknown URIs report false (fail-closed).
class IGrantStore {
 public:
  virtual void persist(const Grant& grant) = 0;
  virtual bool has(const std::string& uri) const = 0;
  virtual std::vector<Grant> restore() const = 0;
  virtual ~IGrantStore() = default;
};

// Permission-minimal invariant: no route requires MANAGE_EXTERNAL_STORAGE.
// Always returns false; scoped-storage routes (app-specific, MediaStore,
// SAF, Photo Picker, FileProvider) never need the broad permission.
bool RequiresManageExternalStorage(const Grant& grant);

// Restart recovery: true iff the store still holds a persisted grant for
// the URI. A simulated restart is a fresh accessor over the same store
// contents; grants never persisted are absent and report false.
bool CanAccessAfterRestart(const IGrantStore& store, const std::string& uri);

}  // namespace muse::net
