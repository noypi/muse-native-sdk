// T6 verification harness (AC-CORE-010): deliberately ABI-incompatible
// test plugin. Wrong major AND wrong hash: the loader's handshake must
// reject this plugin at registration with a clear error and zero crashes
// (no plugin code runs; only the descriptor is read).
//
// Executed inside `./gradlew :engine:core:pluginAbi` (TC-CORE-010).
// Test-only surface exercising the T3/T4/T5 contracts; exposes no new
// production API.
#include "../abi.h"

namespace muse {
namespace core {
namespace plugin {

// Deliberately stale descriptor: major 0 never matches the engine's
// expected major, and the hash is all zeros so ABI_HASH_MISMATCH also
// fires. Expected loader behavior: err(PluginError{
// code: ABI_MAJOR_MISMATCH (or ABI_HASH_MISMATCH first, either is a
// clear expected-vs-actual diagnostic), no crash, plugin body never runs.
const PluginDescriptor kIncompatibleDescriptor = {
    0u,  // abiMajor: intentionally wrong
    0u,  // abiMinor: intentionally wrong
    "0000000000000000000000000000000000000000000000000000000000000000",
    "MusePluginEntry",
};

extern "C" const PluginDescriptor* MusePluginEntry() {
  return &kIncompatibleDescriptor;
}

}  // namespace plugin
}  // namespace core
}  // namespace muse
