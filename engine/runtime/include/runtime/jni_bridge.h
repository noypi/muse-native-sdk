#pragma once

#include <jni.h>

namespace runtime {

// JNI boundary host interface (NFR-RUNTIME-001 / AC-RUNTIME-025).
// JsRuntime-style pure-virtual ABC using only JNI/plain types so consumers
// compile without any engine headers. The engine-backed implementation lives
// in jni_bridge.cpp; every access to the engine goes through opaque handles
// or this interface.
class RuntimeHost {
 public:
  virtual void shutdown() = 0;
  virtual ~RuntimeHost() = default;
};

// Opaque lifecycle across the JNI boundary. Handles are heap-allocated
// engine instances observed only as void*; no engine type appears here.
void* Runtime_create();
void Runtime_destroy(void* handle);

}  // namespace runtime

// JNI entry point: returns JNI_VERSION_1_6 on a valid VM, JNI_ERR otherwise.
// A null VM never yields a version (fail-closed); no exception crosses here.
extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved);
