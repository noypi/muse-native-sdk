// JNI boundary implementation (NFR-RUNTIME-001 / AC-RUNTIME-025).
// This is the only translation unit that knows the engine-backed host type;
// the public header above exposes opaque void* handles plus the RuntimeHost
// ABC, so no engine type reaches JNI consumers.

#include "runtime/jni_bridge.h"

namespace runtime {
namespace {

// Engine-backed host behind the opaque boundary. Kept in this translation
// unit so its type never leaks into any public header.
class EngineRuntimeHost : public RuntimeHost {
 public:
  void shutdown() override {}
};

}  // namespace

void* Runtime_create() {
  // Nothrow allocate: a null return reports failure without an exception
  // crossing the boundary.
  try {
    return new EngineRuntimeHost();
  } catch (...) {
    return nullptr;
  }
}

void Runtime_destroy(void* handle) {
  if (handle == nullptr) {
    return;
  }
  try {
    delete static_cast<EngineRuntimeHost*>(handle);
  } catch (...) {
  }
}

}  // namespace runtime

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  (void)reserved;
  if (vm == nullptr) {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}
