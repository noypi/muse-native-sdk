// Plugin loader handshake implementation (NFR-CORE-006/003).
//
// The loader dlopens the candidate library and resolves only the
// single MusePluginEntry symbol. The reported descriptor is compared
// against the expected one — major (hard reject), minor (hard reject
// per the spec handshake), then the ABI hash — and any mismatch
// returns err(...) before any plugin code runs, with a structured
// diagnostic naming expected vs actual.
#include "loader.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace muse {
namespace core {
namespace plugin {
namespace {

// Canonical entry point name. This is the only symbol the loader
// ever resolves; nothing else in the plugin is looked up or called
// before the handshake passes.
constexpr const char* kEntrySymbol = "MusePluginEntry";

using EntryFn = const PluginDescriptor* (*)();

// Length-limited copy of an abiHash field: actual points into
// plugin-controlled memory and char abiHash[65] is not guaranteed
// NUL-terminated, so no unbounded %s / std::string(char*) may touch it.
std::string CopyHash(const char* hash) {
  if (hash == nullptr) {
    return "";
  }
  size_t len = 0;
  while (len < 64 && hash[len] != '\0') {
    ++len;
  }
  return std::string(hash, len);
}

std::string DescribeMismatch(const char* what, const PluginDescriptor& expected,
                             const PluginDescriptor& actual) {
  const std::string expectedHash = CopyHash(expected.abiHash);
  const std::string actualHash = CopyHash(actual.abiHash);
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "plugin ABI mismatch (%s): expected major=%u minor=%u hash=%.64s, "
                "actual major=%u minor=%u hash=%.64s",
                what, expected.abiMajor, expected.abiMinor,
                expectedHash.c_str(), actual.abiMajor, actual.abiMinor,
                actualHash.c_str());
  return buf;
}

LoadResult Deny(PluginErrorCode code, const PluginDescriptor& expected,
                const PluginDescriptor& actual, void* so) {
  const char* what = "";
  switch (code) {
    case PluginErrorCode::ABI_MAJOR_MISMATCH:
      what = "abiMajor";
      break;
    case PluginErrorCode::ABI_MINOR_MISMATCH:
      what = "abiMinor";
      break;
    case PluginErrorCode::ABI_HASH_MISMATCH:
      what = "abiHash";
      break;
  }
  PluginError error;
  error.code = code;
  error.message = DescribeMismatch(what, expected, actual);
  error.expectedHash = CopyHash(expected.abiHash);
  error.actualHash = CopyHash(actual.abiHash);
  if (so != nullptr) {
    dlclose(so);
  }
  return LoadResult::err(error);
}

}  // namespace

LoadResult LoadResult::ok(void* handle) {
  LoadResult result;
  result.success_ = true;
  result.handle_ = handle;
  return result;
}

LoadResult LoadResult::err(PluginError error) {
  LoadResult result;
  result.success_ = false;
  result.handle_ = nullptr;
  result.error_ = error;
  return result;
}

LoadResult loadPlugin(const char* path, PluginDescriptor expected) {
  void* so = dlopen(path, RTLD_NOW);
  if (so == nullptr) {
    PluginError error;
    error.code = PluginErrorCode::ABI_HASH_MISMATCH;
    error.message =
        std::string("plugin load failed: dlopen failed, expected=") + path +
        " actual=load-error";
    error.expectedHash = CopyHash(expected.abiHash);
    error.actualHash = "";
    return LoadResult::err(error);
  }

  dlerror();
  void* symbol = dlsym(so, kEntrySymbol);
  const char* resolveError = dlerror();
  if (resolveError != nullptr || symbol == nullptr) {
    PluginError error;
    // Missing entry point means the plugin cannot prove its ABI: fail
    // closed under the hash-mismatch code with expected vs actual.
    error.code = PluginErrorCode::ABI_HASH_MISMATCH;
    error.message = std::string("plugin load failed: could not resolve ") +
                    kEntrySymbol + ", expected=" + kEntrySymbol +
                    " actual=missing-symbol";
    error.expectedHash = CopyHash(expected.abiHash);
    error.actualHash = "";
    dlclose(so);
    return LoadResult::err(error);
  }

  // Resolve only MusePluginEntry: the entry function just reports the
  // descriptor. No other plugin code runs until every check below
  // passes.
  auto* entry = reinterpret_cast<EntryFn>(symbol);
  const PluginDescriptor* actual = entry();
  if (actual == nullptr) {
    return Deny(PluginErrorCode::ABI_HASH_MISMATCH, expected,
                PluginDescriptor{0, 0, "", nullptr}, so);
  }

  if (actual->abiMajor != expected.abiMajor) {
    return Deny(PluginErrorCode::ABI_MAJOR_MISMATCH, expected, *actual, so);
  }
  if (actual->abiMinor != expected.abiMinor) {
    return Deny(PluginErrorCode::ABI_MINOR_MISMATCH, expected, *actual, so);
  }
  if (std::strncmp(actual->abiHash, expected.abiHash, 64) != 0) {
    return Deny(PluginErrorCode::ABI_HASH_MISMATCH, expected, *actual, so);
  }

  return LoadResult::ok(so);
}

}  // namespace plugin
}  // namespace core
}  // namespace muse
