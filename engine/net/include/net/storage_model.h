#pragma once

#include <string>

namespace muse::net {

// Android storage-model route selection. Scoped storage is the default:
// app-specific files need no permission, shared media goes through
// MediaStore, user documents through SAF, images through the Photo Picker,
// and outbound shares through FileProvider (content:// URI, never a raw
// file path).
enum class StorageRoute {
  kAppSpecific,
  kMediaStore,
  kSaf,
  kPhotoPicker,
  kFileProvider,
};

// Opaque document handle: the route that owns it plus its URI. URIs are
// synthetic `uri:` strings inbound and `content://` strings outbound.
struct DocRef {
  StorageRoute route = StorageRoute::kAppSpecific;
  std::string uri;
};

// Error shape for storage operations. Backends report failures via return
// values (empty content for a missing read) and never throw.
enum class StorageError {
  kDenied,
  kNotFound,
};

// Storage backend surface. All operations are non-throwing: a read of an
// unknown URI yields empty content, and share routes outbound through
// kFileProvider with a content:// URI.
class IStorageBackend {
 public:
  virtual DocRef open(const std::string& scope) = 0;
  virtual std::string read(const DocRef& ref) = 0;
  virtual void write(const DocRef& ref, const std::string& data) = 0;
  virtual DocRef share(const DocRef& ref) = 0;
  virtual ~IStorageBackend() = default;
};

}  // namespace muse::net
