// JNI boundary implementation (NFR-RUNTIME-001 / AC-RUNTIME-025).
// This is the only translation unit that knows the engine-backed host type;
// the public header above exposes opaque void* handles plus the RuntimeHost
// ABC, so no engine type reaches JNI consumers.

#include "runtime/jni_bridge.h"
#include "runtime/presenter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#endif

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
    // No exception may cross into the VM; report the failure with a
    // diagnostic instead of masking it as success.
    return {PresenterCode::kNoSurface,
            "Runtime_detachSurface: exception during detach; surface state "
            "ambiguous, re-attach before presenting"};
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

// JNI host entries for com.muse.host.MainActivity (Option A native host).
// Spec position: JNI is the Activity/host entry only (FR-CORE-004,
// RULE-CORE-003/004; NFR-RUNTIME-001) — never the frame renderer. The tree
// asset crosses here as bytes; surface attach, page advance, frame raster,
// and detach are thin host-entry gates over gallery_present (pixels stay
// native: ANativeWindow lock/unlock, no View creation, no Java drawing).
// No C++ exception and no pending JNI exception ever escapes into the VM.
// Diagnostics go through __android_log_print (`adb logcat -s muse-host`).

namespace {

#ifdef __ANDROID__
constexpr const char* kMuseLogTag = "muse-host";
// Proof-marker tag: matches the gallery-proof `GalleryPage:I` logcat filter
// (logcat rejects a filter spec with an embedded second colon, so the tag
// itself carries no colon). Markers emitted here mirror exactly what the
// device pager uses — never hardcoded counts.
constexpr const char* kGalleryLogTag = "GalleryPage";

void MuseLogInfo(const std::string& msg) {
  __android_log_print(ANDROID_LOG_INFO, kMuseLogTag, "%s", msg.c_str());
}

void GalleryLogInfo(const std::string& msg) {
  __android_log_print(ANDROID_LOG_INFO, kGalleryLogTag, "%s", msg.c_str());
}
#endif

struct GalleryHandle {
  std::vector<std::uint8_t> tree;
  std::vector<::runtime::GalleryRow> rows;
  std::string parseError;
  bool parsed = false;
  int page = 0;
#ifdef __ANDROID__
  ANativeWindow* window = nullptr;
#endif
  std::mutex mu;
};

GalleryHandle* ToGallery(jlong handle) {
  if (handle == 0) return nullptr;
  return reinterpret_cast<GalleryHandle*>(static_cast<intptr_t>(handle));
}

#ifdef __ANDROID__
// Emits the proof markers the gallery-proof parser scans for
// (`gallery-rows=N gallery-pages=M` pairs and `gallery-page=i/M`), under the
// `GalleryPage` tag so the `GalleryPage:I` logcat filter sees them. Counts
// always come from the live row model + pager (rows.size() and
// GalleryPageCount with the attached window height) — never constants.
// Callers must hold g->mu.
void EmitGalleryMarkersLocked(GalleryHandle* g, int pages, int w, int h) {
  (void)w;
  (void)h;
  char marker[128];
  std::snprintf(marker, sizeof(marker), "gallery-rows=%d gallery-pages=%d",
                static_cast<int>(g->rows.size()), pages);
  GalleryLogInfo(marker);
  char page[64];
  std::snprintf(page, sizeof(page), "gallery-page=%d/%d", g->page + 1, pages);
  GalleryLogInfo(page);
}

// Renders the current page into the attached window. Callers must hold
// g->mu: attach state, page, and rows are all mutated under that lock.
void RenderLocked(GalleryHandle* g) {
  ANativeWindow* win = g->window;
  if (win == nullptr) {
    return;  // No surface yet (or already detached): fail closed, no log
             // spam — surface callbacks re-render once attached.
  }
  ANativeWindow_Buffer buf;
  if (ANativeWindow_lock(win, &buf, nullptr) != 0 || buf.bits == nullptr) {
    MuseLogInfo("nativeRenderFrame: window lock failed; frame skipped");
    return;
  }
  int w = buf.width;
  int h = buf.height;
  int stride = buf.stride;
  int pages = ::runtime::GalleryPageCount(g->rows, h);
  if (pages > 0) {
    g->page %= pages;
    if (g->page < 0) g->page += pages;
  }
  std::string error = g->parseError;
  int drawn = ::runtime::RasterGalleryPage(g->rows, g->page,
                                           static_cast<std::uint32_t*>(buf.bits),
                                           stride, w, h, &error);
  ANativeWindow_unlockAndPost(win);
  // Proof markers for the presented page (device's own row model + pager).
  EmitGalleryMarkersLocked(g, pages, w, h);
  if (!error.empty() || drawn == 0) {
    // Production logging is failure-only: a healthy frame stays silent.
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "nativeRenderFrame: page %d/%d rows=%d drawn=%d "
                  "viewport=%dx%d warn=%s",
                  g->page + 1, pages, static_cast<int>(g->rows.size()),
                  drawn, w, h, error.c_str());
    MuseLogInfo(msg);
  }
}
#endif

}  // namespace

extern "C" MUSE_JNI_EXPORT jlong Java_com_muse_host_MainActivity_nativeLoadTree(
    JNIEnv* env, jobject thiz, jbyteArray treeBytes) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return 0;
  }
  if (treeBytes == nullptr) {
#ifdef __ANDROID__
    MuseLogInfo("nativeLoadTree: null treeBytes -> handle 0");
#endif
    return 0;
  }
  try {
    jsize len = env->GetArrayLength(treeBytes);
    if (env->ExceptionCheck() == JNI_TRUE || len <= 0) {
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
      }
#ifdef __ANDROID__
      MuseLogInfo("nativeLoadTree: empty tree asset -> handle 0");
#endif
      return 0;
    }
    GalleryHandle* g = new GalleryHandle();
    g->tree.resize(static_cast<std::size_t>(len));
    env->GetByteArrayRegion(treeBytes, 0, len,
                            reinterpret_cast<jbyte*>(g->tree.data()));
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
      delete g;
      return 0;
    }
    g->rows = ::runtime::ParseGalleryRows(g->tree.data(), g->tree.size(),
                                          &g->parseError);
    g->parsed = true;
#ifdef __ANDROID__
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "nativeLoadTree: bytes=%d rows=%d%s%s -> handle acquired",
                  static_cast<int>(len), static_cast<int>(g->rows.size()),
                  g->parseError.empty() ? "" : " parseWarn=",
                  g->parseError.empty() ? "" : g->parseError.c_str());
    MuseLogInfo(msg);
    {
      // Proof marker: device's own parsed row count. The window is not
      // attached yet at load, so the pager height is unknown here — the
      // authoritative `gallery-rows=N gallery-pages=M` pair (with the real
      // viewport height) is emitted on every render; this line guarantees
      // the rows count is visible even before the first frame.
      char marker[128];
      std::snprintf(marker, sizeof(marker), "gallery-rows=%d",
                    static_cast<int>(g->rows.size()));
      GalleryLogInfo(marker);
    }
#endif
    return static_cast<jlong>(reinterpret_cast<intptr_t>(g));
  } catch (...) {
    if (env != nullptr && env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return 0;
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeAttachSurface(
    JNIEnv* env, jobject thiz, jlong handle, jobject surface) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(g->mu);
    if (g->window != nullptr) {
      ANativeWindow_release(g->window);
      g->window = nullptr;
    }
    if (surface == nullptr) {
      MuseLogInfo("nativeAttachSurface: null surface; window cleared");
      return;
    }
    g->window = ANativeWindow_fromSurface(env, surface);
    if (g->window == nullptr) {
      MuseLogInfo("nativeAttachSurface: fromSurface failed");
      return;
    }
    // Buffer format is fixed once per surface; the rasterizer writes
    // RGBA_8888 pixels, so every lock sees the same layout.
    if (ANativeWindow_setBuffersGeometry(g->window, 0, 0,
                                         WINDOW_FORMAT_RGBA_8888) != 0) {
      MuseLogInfo("nativeAttachSurface: setBuffersGeometry failed");
      ANativeWindow_release(g->window);
      g->window = nullptr;
      return;
    }
    int w = ANativeWindow_getWidth(g->window);
    int h = ANativeWindow_getHeight(g->window);
    char msg[128];
    std::snprintf(msg, sizeof(msg), "nativeAttachSurface: window %dx%d", w,
                  h);
    MuseLogInfo(msg);
#else
    (void)surface;
#endif
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeRenderFrame(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || handle == 0) {
    return;
  }
  if (env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(g->mu);
    RenderLocked(g);
#endif
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

extern "C" MUSE_JNI_EXPORT jint Java_com_muse_host_MainActivity_nativeAdvancePage(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return 0;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) return 0;
  try {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(g->mu);
    g->page += 1;
    RenderLocked(g);
    {
      // A page turn is a deliberate user action: always worth one line.
      char msg[64];
      std::snprintf(msg, sizeof(msg), "nativeAdvancePage: page %d",
                    g->page + 1);
      MuseLogInfo(msg);
    }
    return static_cast<jint>(g->page);
#else
    return 0;
#endif
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return 0;
  }
}

// JNI touch entry (BUG-M3-FIDELITY-007 T3; FR-INPUT-011 / AC-INPUT-020).
// Java forwards raw MotionEvent fields (action, x, y, pointerId,
// timestampMs); node-id resolution happens natively from laid-out bounds —
// never a pre-decided node id from Java (plan A1). Fail-closed mirror of the
// sibling entries: null env, pending exception, or zero handle yields -1
// with no exception crossing into the VM. Coordinates cross raw and are only
// trusted once the env/handle guards pass.
// The valid-env dispatch core (hit-test/arena/handler via input_chain) and
// the MainActivity.java onTouchEvent forwarding land with T5; until then no
// node claims the tap and this returns -1 after logging the dispatch line
// under the muse-host tag.
extern "C" MUSE_JNI_EXPORT jint Java_com_muse_host_MainActivity_nativeDispatchTouch(
    JNIEnv* env, jobject thiz, jlong handle, jint action, jfloat x, jfloat y,
    jint pointerId, jlong timestampMs) {
  (void)thiz;
  (void)action;
  (void)x;
  (void)y;
  (void)pointerId;
  (void)timestampMs;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return -1;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) {
    return -1;
  }
  try {
#ifdef __ANDROID__
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "nativeDispatchTouch: action=%d x=%.1f y=%.1f pointer=%d",
                  static_cast<int>(action), static_cast<double>(x),
                  static_cast<double>(y), static_cast<int>(pointerId));
    MuseLogInfo(msg);
#else
    (void)g;
#endif
    return -1;
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return -1;
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeDetachSurface(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(g->mu);
    if (g->window != nullptr) {
      ANativeWindow_release(g->window);
      g->window = nullptr;
    }
    MuseLogInfo("nativeDetachSurface: window released");
#endif
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeDestroy(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || handle == 0) {
    return;
  }
  if (env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  GalleryHandle* g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(g->mu);
    if (g->window != nullptr) {
      ANativeWindow_release(g->window);
      g->window = nullptr;
    }
#endif
  } catch (...) {
  }
  try {
    delete g;
  } catch (...) {
  }
}
