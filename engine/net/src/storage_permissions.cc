#include "net/storage_permissions.h"

namespace muse::net {

bool RequiresManageExternalStorage(const Grant& /*grant*/) { return false; }

bool CanAccessAfterRestart(const IGrantStore& store, const std::string& uri) {
  return store.has(uri);
}

}  // namespace muse::net
