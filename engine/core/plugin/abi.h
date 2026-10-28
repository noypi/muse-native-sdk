// Versioned plugin ABI descriptor (NFR-CORE-006, AC-CORE-007).
//
// A plugin exposes exactly one entry point, MusePluginEntry, with C
// linkage. The loader resolves only that symbol and compares the
// reported descriptor against the expected one: major (hard reject),
// minor (hard reject per the spec handshake), then the SHA-256 ABI
// hash over the canonical ABI descriptor, compared as a 64-char hex
// string plus NUL. Any mismatch rejects the plugin before any plugin
// code runs.
#pragma once

#include <cstdint>

namespace muse {
namespace core {
namespace plugin {

// ABI hash: 64 hex chars of SHA-256 over the canonical ABI descriptor
// (major, minor, struct layouts, entry signature), plus the NUL.
struct PluginDescriptor {
  uint32_t abiMajor;
  uint32_t abiMinor;
  char abiHash[65];
  const char* entryPoint;
};

// Mismatch codes for the load-time handshake. Minor mismatch is a hard
// reject, not a warning/compat path.
enum class PluginErrorCode {
  ABI_MAJOR_MISMATCH,
  ABI_MINOR_MISMATCH,
  ABI_HASH_MISMATCH,
};

extern "C" const PluginDescriptor* MusePluginEntry();

}  // namespace plugin
}  // namespace core
}  // namespace muse
