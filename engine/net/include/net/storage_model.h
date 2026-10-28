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
// values and never throw. Encoding: kNotFound is surfaced as empty content
// from read() of an unknown URI; kDenied is surfaced as an empty DocRef::uri
// (or empty shared content) for a route the caller may not use.
enum class StorageError {
  kDenied,
  kNotFound,
};

// Production encoding of StorageError onto the IStorageBackend surface (the
// interface returns DocRef/std::string/void, so errors ride on empty values):
// kNotFound -> read() yields empty content; kDenied -> open()/share() yields
// an empty uri. Referenced here so the enum has a production owner.
constexpr StorageError kEmptyReadError = StorageError::kNotFound;
constexpr StorageError kEmptyUriError = StorageError::kDenied;
static_assert(static_cast<int>(StorageError::kDenied) !=
                  static_cast<int>(StorageError::kNotFound),
              "StorageError encodings must stay distinct");

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
