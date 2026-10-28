#include "net/storage_model.h"

// Pure-interface surface: IStorageBackend has no non-inline members, so
// this translation unit anchors the header for the build. All storage
// semantics (route selection, empty-on-missing reads, FileProvider share)
// live behind the injected backend; no JNI here.

namespace muse::net {
namespace {}  // No non-inline symbols; see storage_model.h.
}  // namespace muse::net
