// JNI boundary implementation (NFR-RUNTIME-001 / AC-RUNTIME-025).
// This is the only translation unit that knows the engine-backed host type;
// the public header above exposes opaque void* handles plus the RuntimeHost
// ABC, so no engine type reaches JNI consumers.

#include "runtime/jni_bridge.h"
#include "runtime/presenter.h"

#include <cstdint>

// The shared Runtime target builds with CXX_VISIBILITY_PRESET hidden, so JNI
// entry points resolved by name lookup must opt back into default visibility.
#define MUSE_JNI_EXPORT __attribute__((visibility("default")))

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

// Opaque surface attach/detach across the JNI boundary (FR-RUNTIME-009 T1).
// The window handle crosses as an opaque void* and is forwarded untouched to
// the presenter; it is never dereferenced as a View here.
PresenterError Runtime_attachSurface(void* opaque_window) {
  try {
    return AttachSurface(opaque_window);
  } catch (...) {
    return {PresenterCode::kInvalidWindow,
            "Runtime_attachSurface: exception during attach"};
  }
}

PresenterError Runtime_detachSurface() {
  try {
    return DetachSurface();
  } catch (...) {
    return {PresenterCode::kOk, ""};
  }
}

}  // namespace runtime

MUSE_JNI_EXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  (void)reserved;
  if (vm == nullptr) {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}

// JNI glue for com.muse.devicelab.EngineHost (device-lab lane).
// Delegates to Runtime_create/Runtime_destroy with null/exception
// fail-closed semantics: a null JNIEnv, a pending exception, or any C++
// exception yields 0 / no-op and never lets an exception cross into the VM.
// No engine type appears here; handles cross as opaque jlong slots.
extern "C" MUSE_JNI_EXPORT jlong Java_com_muse_devicelab_EngineHost_nativeCreate(
    JNIEnv* env, jobject thiz) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return 0;
  }
  try {
    void* handle = ::runtime::Runtime_create();
    if (handle == nullptr) {
      return 0;
    }
    return static_cast<jlong>(reinterpret_cast<intptr_t>(handle));
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return 0;
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_devicelab_EngineHost_nativeDestroy(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || handle == 0) {
    return;
  }
  if (env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  try {
    ::runtime::Runtime_destroy(
        reinterpret_cast<void*>(static_cast<intptr_t>(handle)));
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

// JNI glue for opaque surface attach/detach (FR-RUNTIME-009 T1).
// The window handle crosses as an opaque jlong slot; it is never
// dereferenced as a View. Fail-closed: a null JNIEnv, a pending exception,
// or a zero handle yields the corresponding error code and never throws
// across into the VM. Returns the PresenterCode as jint.
extern "C" MUSE_JNI_EXPORT jint Java_com_muse_devicelab_EngineHost_nativeAttachSurface(
    JNIEnv* env, jobject thiz, jlong windowHandle) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return static_cast<jint>(::runtime::PresenterCode::kInvalidWindow);
  }
  try {
    ::runtime::PresenterError err = ::runtime::Runtime_attachSurface(
        reinterpret_cast<void*>(static_cast<intptr_t>(windowHandle)));
    return static_cast<jint>(err.code);
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return static_cast<jint>(::runtime::PresenterCode::kInvalidWindow);
  }
}

extern "C" MUSE_JNI_EXPORT jint Java_com_muse_devicelab_EngineHost_nativeDetachSurface(
    JNIEnv* env, jobject thiz) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return static_cast<jint>(::runtime::PresenterCode::kInvalidWindow);
  }
  try {
    ::runtime::PresenterError err = ::runtime::Runtime_detachSurface();
    return static_cast<jint>(err.code);
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return static_cast<jint>(::runtime::PresenterCode::kInvalidWindow);
  }
}
