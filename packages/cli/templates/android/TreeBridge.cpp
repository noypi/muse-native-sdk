// JNI tree-render bridge for com.muse.host.MainActivity (Option A native host).
//
// Reuses the engine/runtime jni_bridge pattern from
// engine/runtime/src/jni_bridge.cpp: opaque handles via Runtime_create /
// Runtime_destroy plus the JNI_OnLoad version gate. No engine type crosses
// this boundary; handles travel as opaque jlong slots. No C++ exception and
// no pending JNI exception ever escapes into the VM: every failure returns 0
// (load) or no-ops (render).
//
// Note: the JNI load entry lives solely in engine/runtime/src/jni_bridge.cpp
// (single-definition link, null-VM fail-closed gate kept there); this
// template declares only the runtime lifecycle below and defines no load
// entry, so it links against the engine symbol without duplicating it.

#include <jni.h>

#include <cstdint>

// Reuse the engine/runtime lifecycle (see engine/runtime jni_bridge).
// Declared here so this template compiles without engine headers; the real
// symbols resolve from libRuntime.so at link time.
namespace runtime {
void* Runtime_create();
void Runtime_destroy(void* handle);
}  // namespace runtime

extern "C" jlong Java_com_muse_host_MainActivity_nativeLoadTree(
    JNIEnv* env, jobject thiz, jbyteArray treeBytes) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return 0;
  }
  if (treeBytes == nullptr) {
    return 0;
  }
  try {
    jsize len = env->GetArrayLength(treeBytes);
    if (env->ExceptionCheck() == JNI_TRUE || len <= 0) {
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
      }
      return 0;
    }
    // Pin the bytes only to validate them; the engine owns its own copy.
    jboolean isCopy = JNI_FALSE;
    jbyte* data =
        static_cast<jbyte*>(env->GetPrimitiveArrayCritical(treeBytes, &isCopy));
    if (data == nullptr) {
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
      }
      return 0;
    }
    bool empty = (len == 0 || data[0] == 0);
    env->ReleasePrimitiveArrayCritical(treeBytes, data, JNI_ABORT);
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
      return 0;
    }
    if (empty) {
      return 0;
    }
    void* handle = ::runtime::Runtime_create();
    if (handle == nullptr) {
      return 0;
    }
    return static_cast<jlong>(reinterpret_cast<intptr_t>(handle));
  } catch (...) {
    if (env != nullptr && env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return 0;
  }
}

extern "C" void Java_com_muse_host_MainActivity_nativeRenderFrame(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || handle == 0) {
    return;
  }
  if (env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  try {
    if (handle == 0) {
      return;
    }
    // Frame rendering for the engine handle happens here once the surface
    // attachment lands; until then this is a validated no-op gate so a
    // zero or stale handle can never crash the VM.
    (void)handle;
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}
