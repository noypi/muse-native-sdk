// Load-time plugin handshake (NFR-CORE-003, AC-CORE-004).
//
// loadPlugin() dlopens the candidate .so, resolves only the single
// MusePluginEntry symbol, and rejects major/minor/hash mismatches
// before any plugin code runs. Success returns ok(handle); every
// mismatch returns err(PluginError) with a structured diagnostic
// naming the expected vs actual descriptor.
#pragma once

#include <string>

#include "abi.h"

namespace muse {
namespace core {
namespace plugin {

// Structured load failure: machine-readable code plus a diagnostic
// carrying both sides of the comparison.
struct PluginError {
  PluginErrorCode code;
  std::string message;
  std::string expectedHash;
  std::string actualHash;
};

// Load outcome: ok(handle) on a compatible plugin, err(PluginError)
// on any handshake mismatch.
class LoadResult {
 public:
  static LoadResult ok(void* handle);
  static LoadResult err(PluginError error);

  bool success() const { return success_; }
  void* handle() const { return handle_; }
  const PluginError& error() const { return error_; }

 private:
  bool success_ = false;
  void* handle_ = nullptr;
  PluginError error_;
};

LoadResult loadPlugin(const char* path, PluginDescriptor expected);

}  // namespace plugin
}  // namespace core
}  // namespace muse
