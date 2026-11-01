// JNI boundary implementation (NFR-RUNTIME-001 / AC-RUNTIME-025).
// This is the only translation unit that knows the engine-backed host type;
// the public header above exposes opaque void* handles plus the RuntimeHost
// ABC, so no engine type reaches JNI consumers.

#include "runtime/jni_bridge.h"
#include "runtime/presenter.h"
#include "runtime/egl_blit.h"
#include "runtime/renderer_preference.h"
// FR-GRAPHICS-014 T2 (review MAJOR 2): shared Skia-leg declaration. The
// block-scope forward declaration is replaced by the real header so the
// `PresentSkiaFrameWithFallback` call below binds to the AC-GRAPHICS-039
// contract in `engine/graphics/skia/present_skia.h` (same shape as the
// `egl_blit.h` header above); no signature drift possible.
#include "engine/graphics/skia/present_skia.h"
// FR-GRAPHICS-012 T3: single-definition compile of the fallback branch.
// `src/egl_blit.cpp` is not listed in any CMake target (header-only project
// pattern, see that TU's note); including it here compiles the
// `PresentFrameWithFallback` definition exactly once into `runtime` so the
// hermetic `present_fallback` test links, with no build-file change.
#include "egl_blit.cpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/input_chain.h"

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#endif

// BUG-M3-FIDELITY-012 T8: provenance macros baked by engine/runtime
// CMakeLists.txt (T7). Safe "unknown" defaults so this TU compiles even
// when the macros are absent (non-git configure, other build lanes).
#ifndef ENGINE_GIT_SHA
#define ENGINE_GIT_SHA "unknown"
#endif
#ifndef GALLERY_PRESENT_SHA
#define GALLERY_PRESENT_SHA "unknown"
#endif

// The shared Runtime target builds with CXX_VISIBILITY_PRESET hidden, so JNI
// entry points resolved by name lookup must opt back into default visibility.
#define MUSE_JNI_EXPORT __attribute__((visibility("default")))

namespace runtime {
// BUG-INPUT-001 T3: layering-page predicate + pager owned by the present path
// (defined in gallery_present.cpp, deliberately no header change). Forward-
// declared here so the dispatch slice stays page-scoped without widening the
// production claim to a header.
std::vector<int> PageStarts(const std::vector<GalleryRow>& rows, int height);
bool IsLayeringPageSlice(const std::vector<GalleryRow>& rows, int first,
                         int last);
}  // namespace runtime

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

// T2 (BUG-M3-FIDELITY-003 / FR-M3-006): JNI-lane view of the live probe
// field. The IME/touch entries consult this before routing composition or
// caret events at the probe page: while no live field is attached the static
// box painter stands in (fail-closed, never a phantom editable). Consumes the
// RULE-M3-004 attach gate (FR-TEXT-008/009/013, FR-LAYOUT-012) without
// reimplementing it. Never throws across the VM boundary.
bool LiveTextProbeAttachedForIme() noexcept {
  try {
    return ::liveKindOverrideActive();
  } catch (...) {
    return false;
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

// T3a (BUG-M3-FIDELITY-002 / AC-M3-010): same-activity theme override.
//
// The gallery rows carry a baked light/dark section marker, and the retained
// emit path paints whatever palette the row descriptors resolve to —
// `cmd uimode` therefore re-renders byte-identical pixels (T3 blocker). This
// process-wide override flips the presented palette in place on the same
// GalleryHandle/page: when set, every light-baseline ink the window sink
// submits is remapped to its M3 dark-baseline counterpart before it lands in
// the locked window. Same activity, same page, no restart, no lifecycle
// change; `cmd uimode` delivery itself is untouched.
//
// Only fully-opaque baseline inks remap (alpha rides through unchanged, so
// scrims/washes keep their coverage); anything unrecognized passes through,
// so a future palette addition can never blank the frame.
constexpr std::uint32_t ThemePx(unsigned r, unsigned g, unsigned b) {
  // 24-bit RGB only: callers compare against the alpha-stripped payload RGB
  // and recombine the original alpha afterwards.
  return static_cast<std::uint32_t>(r & 0xFFu) |
         (static_cast<std::uint32_t>(g & 0xFFu) << 8) |
         (static_cast<std::uint32_t>(b & 0xFFu) << 16);
}

std::atomic<int> g_nightMode{0};

std::uint32_t ThemeInk(std::uint32_t c) {
  if (g_nightMode.load(std::memory_order_relaxed) == 0) return c;
  const unsigned a = (c >> 24) & 0xFFu;
  if (a == 0) return c;
  const std::uint32_t rgb = c & 0x00FFFFFFu;
  std::uint32_t out = rgb;
  // M3 baseline light -> dark pairs (opaque RGB only; alpha recombined).
  if (rgb == ThemePx(0xFE, 0xF7, 0xFF))
    out = ThemePx(0x14, 0x12, 0x18);  // surface
  else if (rgb == ThemePx(0xF7, 0xF2, 0xFA))
    out = ThemePx(0x1D, 0x1B, 0x20);  // card bg -> dark surfaceContainer
  else if (rgb == ThemePx(0x67, 0x50, 0xA4))
    out = ThemePx(0xD0, 0xBC, 0xFF);  // primary
  else if (rgb == ThemePx(0xFF, 0xFF, 0xFF))
    out = ThemePx(0x38, 0x1E, 0x72);  // onPrimary
  else if (rgb == ThemePx(0x1D, 0x1B, 0x20))
    out = ThemePx(0xE6, 0xE0, 0xE9);  // onSurface / footer bg
  else if (rgb == ThemePx(0xE8, 0xDE, 0xF8))
    out = ThemePx(0x4F, 0x37, 0x8B);  // secondaryContainer
  else if (rgb == ThemePx(0x4F, 0x37, 0x8B))
    out = ThemePx(0xE8, 0xDE, 0xF8);  // onPrimaryContainer -> dark onSecCont
  else if (rgb == ThemePx(0xE7, 0xE0, 0xEC))
    out = ThemePx(0x49, 0x45, 0x4F);  // surfaceVariant
  else if (rgb == ThemePx(0x49, 0x45, 0x4F))
    out = ThemePx(0xCA, 0xC4, 0xD0);  // onSurfaceVariant
  else if (rgb == ThemePx(0xCA, 0xC4, 0xD0))
    out = ThemePx(0x49, 0x45, 0x4F);  // outlineVariant
  else if (rgb == ThemePx(0x79, 0x74, 0x7E))
    out = ThemePx(0x93, 0x8F, 0x99);  // outline
  else if (rgb == ThemePx(0xFF, 0xD8, 0xE4))
    out = ThemePx(0x63, 0x3B, 0x48);  // tertiaryContainer
  else if (rgb == ThemePx(0xBA, 0x1A, 0x1A))
    out = ThemePx(0xFF, 0xB4, 0xAB);  // error
  else if (rgb == ThemePx(0x32, 0x2F, 0x35))
    out = ThemePx(0xE6, 0xE0, 0xE9);  // inverseSurface
  return out | (static_cast<std::uint32_t>(a) << 24);
}

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
  // Device tap chain state (BUG-M3-FIDELITY-007 T5): layout-bounds hit-test
  // tree plus the gesture arena the valid-env dispatch body drives. The
  // tree is populated from the layout engine's laid-out bounds; until then
  // it is empty and every tap resolves -1 (fail closed, never a phantom
  // node). Guarded by mu like the rest of the handle.
  HitTestTree tapTree;
  GestureArena tapArena;
  // BUG-INPUT-001 T3 (AC-M3-022 scrim clause): live overlay count for the
  // layering page (paintLayeringPage presents the scrim + five contents, so
  // the stack opens at 5) and the last viewport height RenderLocked presented
  // with, so the ACTION_UP slice can scope dismiss routing to the layering
  // page. Guarded by mu like the rest of the handle.
  int layeringOverlayCount = 5;
  int lastViewportH = 0;
  // BUG-INPUT-001 T4: last page RenderLocked presented; a page change resets
  // layeringOverlayCount to the full stack (5) so away/back re-entry restarts
  // full. Guarded by mu like the rest of the handle.
  int lastPresentedPage = -1;
#ifdef __ANDROID__
  ANativeWindow* window = nullptr;
  // FR-STARTUP-003 T1 (RULE-STARTUP-002): single-owner render-thread frame
  // resources. Owned solely by the render thread, touched only with g->mu
  // held. `staged` is resized only when stride*h outgrows capacity;
  // `renderer` is constructed once from DeviceCaps{1,1}. Never touched from
  // any other thread (the T2 enqueue-and-return lane never renders inline).
  std::vector<std::uint32_t> staged;
  std::optional<::runtime::Renderer> renderer;
  // FR-STARTUP-003 T2 (AC-STARTUP-005): one render thread + single-pending
  // frame queue (coalesce, not backlog). The render_thread owns every
  // ANativeWindow lock/raster (RenderLocked runs only there); JNI entries on
  // the UI thread only EnqueueFrame under the short qmu critical section and
  // return immediately. `pendingFrame` coalesces rapid calls to at most one
  // extra present (no unbounded queue growth). Guarded by qmu, never by
  // g->mu, so the enqueue never waits on a frame in flight.
  std::thread render_thread;
  std::mutex qmu;
  std::condition_variable qcv;
  bool pendingFrame = false;
  bool renderShutdown = false;
  bool renderStarted = false;
  ~GalleryHandle() {
    // Teardown drain: cancel the pending frame and stop the render thread
    // before member teardown, so no draw runs on a dead window (RULE-004).
    // Never throws (destructor path, JNI never observes it).
    try {
      {
        std::lock_guard<std::mutex> ql(qmu);
        renderShutdown = true;
        pendingFrame = false;
      }
      qcv.notify_all();
      if (render_thread.joinable()) {
        if (render_thread.get_id() != std::this_thread::get_id()) {
          render_thread.join();
        } else {
          render_thread.detach();
        }
      }
    } catch (...) {
    }
  }
#endif
  std::mutex mu;
};

// Validated handle registry (cleared-review SECURITY MEDIUM, binding a;
// re-audit MEDIUM fix: counted lifetime for the ToGallery waiter):
// the raw jlong slot is never cast-and-dereferenced blindly. Only handles
// created by nativeLoadTree and still live resolve; forged/stale/zero
// handles fail closed (nullptr → -1/no-op, no deref). The registry holds
// one shared-ownership ref per live handle; every lookup returns a counted
// reference the caller holds across its g->mu critical section, so a racing
// destroy (erase + teardown) can never free a handle an earlier holder is
// about to lock: destroy drops only the registry ref and the object dies
// when the last in-flight ref drains. Unregister happens exactly once in
// nativeDestroy, which also zeroes the Java-side slot
// (MainActivity.onDestroy sets engineHandle = 0 after the call).
std::mutex g_handleRegistryMu;
std::unordered_map<GalleryHandle*, std::shared_ptr<GalleryHandle>>
    g_liveHandles;

void RegisterHandle(const std::shared_ptr<GalleryHandle>& g) {
  if (g == nullptr) return;
  std::lock_guard<std::mutex> lock(g_handleRegistryMu);
  g_liveHandles.emplace(g.get(), g);
}

// Counted lookup: returns a shared ref held across the caller's g->mu
// critical section (never a bare pointer outliving the registry lock).
std::shared_ptr<GalleryHandle> ToGallery(jlong handle) {
  if (handle == 0) return nullptr;
  GalleryHandle* raw =
      reinterpret_cast<GalleryHandle*>(static_cast<intptr_t>(handle));
  std::lock_guard<std::mutex> lock(g_handleRegistryMu);
  const auto it = g_liveHandles.find(raw);
  return it != g_liveHandles.end() ? it->second : nullptr;
}

// Atomic destroy claim (SECURITY MEDIUM): lookup + erase under the one
// registry lock, handing the live handle out exactly once. A second destroy
// (or any racing entry) finds the handle already erased and fails closed,
// so teardown below runs exclusively and re-entry during destroy is barred.
// New dispatches fail closed too (erased from the live set); an in-flight
// dispatch keeps its own counted ref, so the object outlives this destroy
// until that critical section drains.
std::shared_ptr<GalleryHandle> ClaimForDestroy(jlong handle) {
  if (handle == 0) return nullptr;
  GalleryHandle* raw =
      reinterpret_cast<GalleryHandle*>(static_cast<intptr_t>(handle));
  std::lock_guard<std::mutex> lock(g_handleRegistryMu);
  const auto it = g_liveHandles.find(raw);
  if (it == g_liveHandles.end()) {
    return nullptr;
  }
  std::shared_ptr<GalleryHandle> claimed = std::move(it->second);
  g_liveHandles.erase(it);
  return claimed;
}

#ifdef __ANDROID__
// Emits the proof markers the gallery-proof parser scans for
// (`gallery-rows=N gallery-pages=M` pairs and `gallery-page=i/M`), under the
// `GalleryPage` tag so the `GalleryPage:I` logcat filter sees them. Counts
// always come from the live row model + pager (rows.size() and
// GalleryPageCount with the attached window height) — never constants.
// Callers must hold a counted registry ref and g->mu.
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

// Window-attached Swapchain for the T10 presented-window sink (F1 fix).
// Wraps the locked ANativeWindow RGBA_8888 buffer: every DrawCmd submitted
// through Renderer draw paints its bounds into the window, so the presented
// pixels come from the retained Swapchain output (EmitGalleryWindowCmds
// over the T5 ShapedText emission), never from the CPU RasterGalleryPage
// oracle. Solid-color kinds fill their payload color; ShapedText and Text
// runs paint their text-color footprint once T6 RasterizeShapedText accepts
// the run in bounds (dangling runs paint nothing: the renderer already
// rejects run 0 upstream, so the mirror never invents pixels for a rejected
// run). This CPU mirror owns the window footprint only; exact glyph
// compositing is the compositor Swapchain lane. Pure C++; zero JNI on the
// frame path.
constexpr std::uint32_t WindowRGBA(std::uint8_t r, std::uint8_t g,
                                    std::uint8_t b, std::uint8_t a) {
  return static_cast<std::uint32_t>(r) |
         (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}

class WindowSwapchain : public ::runtime::Swapchain {
 public:
  WindowSwapchain(std::uint32_t* bits, int stridePx, int width, int height)
      : bits_(bits), stride_(stridePx), w_(width), h_(height) {
    // The retained canvas is fixed at the present geometry (720x1600, the
    // same reference PresentGallery uses); map it onto the locked window.
    sx_ = (w_ > 0) ? static_cast<float>(w_) / 720.0f : 1.0f;
    sy_ = (h_ > 0) ? static_cast<float>(h_) / 1600.0f : 1.0f;
  }

  void submit(const ::runtime::DrawCmd& cmd) override {
    if (bits_ == nullptr) return;
    // T3a theme override: glyph-run ink resolves here (payload textColor),
    // so the remap applies before blending — never a whole-bounds fill.
    // T11 fix 1: ShapedText/Text runs blend their real glyph coverage
    // per pixel inside cmd.bounds (threaded out of RasterizeShapedText) —
    // never a solid text-color footprint. Dangling runs (id 0 or
    // unresolvable) yield no coverage and paint nothing (fail-closed,
    // mirroring the renderer).
    if ((cmd.kind == ::runtime::PrimitiveKind::ShapedText ||
         cmd.kind == ::runtime::PrimitiveKind::Text)) {
      if (cmd.payload.glyphRunId == 0) return;
      const ::muse::text::StatusOr<::runtime::CoverageMask> coverage =
          ::runtime::RasterizeShapedText(cmd.payload.glyphRunId, cmd.bounds,
                                         cmd.payload.textColor);
      if (!coverage.ok()) return;
      const std::uint32_t ink = ThemeInk(cmd.payload.textColor != 0
                                     ? cmd.payload.textColor
                                     : WindowRGBA(0x1D, 0x1B, 0x20, 0xFF));
      BlendMask(*coverage, cmd.bounds, ink);
      return;
    }
    // T11 fix 3: the ElevationShadow wash paints as two offset soft passes
    // (mirroring the retained-path ShadowRounded reference: +2/+3 at a=28,
    // +4/+6 at a=18), clipped to the rounded silhouette — never skipped.
    if (cmd.kind == ::runtime::PrimitiveKind::ElevationShadow) {
      if (cmd.payload.elevationDp <= 0.0f) return;
      const unsigned sa = (cmd.payload.shadowColor >> 24) & 0xFFu;
      if (sa == 0) return;
      WashShadow(cmd, 2.0f, 3.0f, 28);
      WashShadow(cmd, 4.0f, 6.0f, 18);
      return;
    }
    const std::uint32_t ink = InkFor(cmd);
    if ((ink >> 24) == 0) return;
    FillBounds(cmd.bounds, ink);
  }

  void clear(std::uint32_t color) {
    if (bits_ == nullptr) return;
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        bits_[y * stride_ + x] = color;
      }
    }
  }

 private:
  static std::uint32_t InkFor(const ::runtime::DrawCmd& cmd) {
    using ::runtime::PrimitiveKind;
    std::uint32_t ink = WindowRGBA(0xF7, 0xF2, 0xFA, 0xFF);
    switch (cmd.kind) {
      case PrimitiveKind::Text:
      case PrimitiveKind::ShapedText:
        ink = cmd.payload.textColor != 0
                  ? cmd.payload.textColor
                  : WindowRGBA(0x1D, 0x1B, 0x20, 0xFF);
        break;
      case PrimitiveKind::StateLayer:
        ink = cmd.payload.stateColor != 0
                  ? cmd.payload.stateColor
                  : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
        break;
      case PrimitiveKind::Ripple:
        ink = cmd.payload.rippleColor != 0
                  ? cmd.payload.rippleColor
                  : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
        break;
      case PrimitiveKind::VectorIcon:
        ink = cmd.payload.tintColor != 0
                  ? cmd.payload.tintColor
                  : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
        break;
      case PrimitiveKind::CircleArc:
        ink = cmd.payload.arcColor != 0
                  ? cmd.payload.arcColor
                  : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
        break;
      case PrimitiveKind::OverlayScrim: {
        const float alpha = cmd.payload.scrimAlpha;
        const unsigned a = (alpha <= 0.0f)
                               ? 0u
                               : (alpha >= 1.0f)
                                     ? 255u
                                     : static_cast<unsigned>(255.0f * alpha +
                                                             0.5f);
        const std::uint32_t c = cmd.payload.scrimColor;
        ink = (c & 0x00FFFFFFu) |
               (static_cast<std::uint32_t>(a & 0xFFu) << 24);
        break;
      }
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
        // T11 fix 3: card/band fills. A nonzero payload selects the
        // emitted fill; payload-0 selects the real default tonal card
        // fill (M3 surface-variant), never background-on-background.
        if (cmd.payload.stateColor != 0) {
          ink = cmd.payload.stateColor;
        } else {
          ink = WindowRGBA(0xE7, 0xE0, 0xEC, 0xFF);
        }
        break;
      default:
        break;
    }
    // T3a: same-activity theme override applied at the single ink choke
    // point — every submitted fill/text/tint/scim resolves here.
    return ThemeInk(ink);
  }

  // T11 fix 1: src-over blend one coverage mask inside bounds. Mask texels
  // map proportionally onto the scaled destination rect; pixels outside
  // the mask stay untouched (no whole-bounds fill).
  void BlendMask(const ::runtime::CoverageMask& mask,
                 ::runtime::Rect bounds, std::uint32_t src) {
    if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) return;
    int x0 = static_cast<int>(bounds.x * sx_);
    int y0 = static_cast<int>(bounds.y * sy_);
    int x1 = static_cast<int>((bounds.x + bounds.w) * sx_ + 0.5f);
    int y1 = static_cast<int>((bounds.y + bounds.h) * sy_ + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w_) x1 = w_;
    if (y1 > h_) y1 = h_;
    if (x1 <= x0 || y1 <= y0) return;
    const unsigned sr = src & 0xFFu;
    const unsigned sg = (src >> 8) & 0xFFu;
    const unsigned sb = (src >> 16) & 0xFFu;
    for (int y = y0; y < y1; ++y) {
      const int my = static_cast<int>(
          (static_cast<double>(y - y0) * mask.height) / (y1 - y0));
      for (int x = x0; x < x1; ++x) {
        const int mx = static_cast<int>(
            (static_cast<double>(x - x0) * mask.width) / (x1 - x0));
        const std::size_t t = static_cast<std::size_t>(my) *
                                  static_cast<std::size_t>(mask.width) +
                              static_cast<std::size_t>(mx);
        if (t >= mask.alpha.size()) continue;
        const float a = mask.alpha[t];
        if (a <= 0.0f) continue;
        const std::uint32_t dst = bits_[y * stride_ + x];
        const unsigned dr = dst & 0xFFu;
        const unsigned dg = (dst >> 8) & 0xFFu;
        const unsigned db = (dst >> 16) & 0xFFu;
        const unsigned r =
            static_cast<unsigned>(sr * a + dr * (1.0f - a) + 0.5f);
        const unsigned g =
            static_cast<unsigned>(sg * a + dg * (1.0f - a) + 0.5f);
        const unsigned b =
            static_cast<unsigned>(sb * a + db * (1.0f - a) + 0.5f);
        bits_[y * stride_ + x] =
            static_cast<std::uint32_t>(r > 255u ? 255u : r) |
            (static_cast<std::uint32_t>(g > 255u ? 255u : g) << 8) |
            (static_cast<std::uint32_t>(b > 255u ? 255u : b) << 16) |
            (static_cast<std::uint32_t>(0xFF) << 24);
      }
    }
  }

  // T11 fix 3: one soft shadow pass at (dx, dy) with effective alpha `a`.
  // Clipped to the rounded silhouette (same corner test as the
  // retained-path ShadowRounded reference); src-over blended.
  void WashShadow(const ::runtime::DrawCmd& cmd, float dx, float dy,
                  unsigned a) {
    const ::runtime::Rect b = cmd.bounds;
    const float rad = cmd.radius;
    // T3a: shadow ink rides the same theme override as every other ink.
    const std::uint32_t shadow = ThemeInk(cmd.payload.shadowColor);
    const std::uint32_t c = shadow;
    const unsigned sr = c & 0xFFu;
    const unsigned sg = (c >> 8) & 0xFFu;
    const unsigned sb = (c >> 16) & 0xFFu;
    int x0 = static_cast<int>((b.x + dx - 6.0f) * sx_);
    int y0 = static_cast<int>((b.y + dy - 6.0f) * sy_);
    int x1 = static_cast<int>((b.x + b.w + dx + 6.0f) * sx_ + 0.5f);
    int y1 = static_cast<int>((b.y + b.h + dy + 6.0f) * sy_ + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w_) x1 = w_;
    if (y1 > h_) y1 = h_;
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0; y < y1; ++y) {
      const float vy = sy_ > 0.0f ? static_cast<float>(y) / sy_ : 0.0f;
      for (int x = x0; x < x1; ++x) {
        const float vx = sx_ > 0.0f ? static_cast<float>(x) / sx_ : 0.0f;
        // Rounded-silhouette clip in canvas px (offset wash rect).
        const float ox = vx - dx;
        const float oy = vy - dy;
        float cx = ox;
        if (ox < b.x + rad) cx = b.x + rad;
        else if (ox > b.x + b.w - rad) cx = b.x + b.w - rad;
        float cy = oy;
        if (oy < b.y + rad) cy = b.y + rad;
        else if (oy > b.y + b.h - rad) cy = b.y + b.h - rad;
        const float ddx = ox - cx;
        const float ddy = oy - cy;
        if (rad > 0.0f && ddx * ddx + ddy * ddy > rad * rad) continue;
        if (ox < b.x - 6.0f || ox > b.x + b.w + 6.0f ||
            oy < b.y - 6.0f || oy > b.y + b.h + 6.0f) {
          continue;
        }
        const std::uint32_t dst = bits_[y * stride_ + x];
        const unsigned dr = dst & 0xFFu;
        const unsigned dg = (dst >> 8) & 0xFFu;
        const unsigned db = (dst >> 16) & 0xFFu;
        const unsigned r = (sr * a + dr * (255u - a)) / 255u;
        const unsigned g = (sg * a + dg * (255u - a)) / 255u;
        const unsigned bl = (sb * a + db * (255u - a)) / 255u;
        bits_[y * stride_ + x] =
            static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8) |
            (static_cast<std::uint32_t>(bl) << 16) |
            (static_cast<std::uint32_t>(0xFF) << 24);
      }
    }
  }

  void FillBounds(::runtime::Rect bounds, std::uint32_t src) {    int x0 = static_cast<int>(bounds.x * sx_);
    int y0 = static_cast<int>(bounds.y * sy_);
    int x1 = static_cast<int>((bounds.x + bounds.w) * sx_ + 0.5f);
    int y1 = static_cast<int>((bounds.y + bounds.h) * sy_ + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w_) x1 = w_;
    if (y1 > h_) y1 = h_;
    if (x1 <= x0 || y1 <= y0) return;
    const unsigned sa = (src >> 24) & 0xFFu;
    const unsigned sr = src & 0xFFu;
    const unsigned sg = (src >> 8) & 0xFFu;
    const unsigned sb = (src >> 16) & 0xFFu;
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        const std::uint32_t dst = bits_[y * stride_ + x];
        const unsigned dr = dst & 0xFFu;
        const unsigned dg = (dst >> 8) & 0xFFu;
        const unsigned db = (dst >> 16) & 0xFFu;
        const unsigned r = (sr * sa + dr * (255u - sa)) / 255u;
        const unsigned g = (sg * sa + dg * (255u - sa)) / 255u;
        const unsigned b = (sb * sa + db * (255u - sa)) / 255u;
        bits_[y * stride_ + x] =
            static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8) |
            (static_cast<std::uint32_t>(b) << 16) |
            (static_cast<std::uint32_t>(0xFF) << 24);
      }
    }
  }

  std::uint32_t* bits_;
  int stride_;
  int w_;
  int h_;
  float sx_;
  float sy_;
};

// FR-STARTUP-003 T1 (RULE-STARTUP-002): render-thread-only frame resource
// gate. Callers must hold g->mu on the render thread. Ensures the reused
// `g->renderer` (constructed once from DeviceCaps{1,1}) exists and the reused
// `g->staged` buffer covers stride*h, resizing only when the need outgrows
// capacity. Returns false = geometry invalid or allocation failed: the frame
// is skipped and the last good frame is kept. Never throws.
bool EnsureFrameResourcesLocked(GalleryHandle* g, int stride, int h) noexcept {
  try {
    if (g == nullptr || stride <= 0 || h <= 0) {
      return false;
    }
    if (!g->renderer.has_value()) {
      ::runtime::DeviceCaps baselineCaps;
      baselineCaps.apiMajor = 1;
      baselineCaps.apiMinor = 1;
      g->renderer.emplace(baselineCaps);
    }
    const std::size_t need = static_cast<std::size_t>(stride) *
                             static_cast<std::size_t>(h);
    if (need == 0) {
      return false;
    }
    if (g->staged.size() < need) {
      g->staged.resize(need);
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Renders the current page into the attached window. Callers must hold
// g->mu: attach state, page, and rows are all mutated under that lock.
// FR-STARTUP-003 T3 (AC-STARTUP-006, RULE-STARTUP-001/002) drop-beats-tear:
// every drop path leaves the locked bits untouched and keeps the previous
// posted frame standing — null/detached window returns silent (no draw, no
// crash); lock-fail / null-bits logs one
// "nativeRenderFrame: window lock failed; frame skipped" line and returns
// without unlockAndPost of partial bits; presented==false skips the
// staged->locked copy (bits untouched) and unlockAndPost reposts the previous
// frame; markers still emit from the live row model on every locked present.
void RenderLocked(GalleryHandle* g) {
  ANativeWindow* win = g->window;
  if (win == nullptr) {
    return;  // No surface yet (or already detached): fail closed, no log
             // spam — surface callbacks re-render once attached.
  }
  // Value-init so the lock-fail path sees null bits even if the platform
  // leaves buf untouched on failure (never read indeterminate bits).
  ANativeWindow_Buffer buf{};
  if (ANativeWindow_lock(win, &buf, nullptr) != 0 || buf.bits == nullptr) {
    MuseLogInfo("nativeRenderFrame: window lock failed; frame skipped");
    return;
  }
  int w = buf.width;
  int h = buf.height;
  int stride = buf.stride;
  // BUG-INPUT-001 T3: remember the presented viewport height (mu is held by
  // the caller) so the ACTION_UP slice can window the layering predicate.
  g->lastViewportH = h;
  int pages = ::runtime::GalleryPageCount(g->rows, h);
  if (pages > 0) {
    g->page %= pages;
    if (g->page < 0) g->page += pages;
  }
  // F1 sink fix: the presented window is driven by the retained Swapchain
  // text output, not the CPU oracle. Present the device gallery page
  // through Renderer draw into the window-attached Swapchain; the CPU
  // RasterGalleryPage oracle is no longer the presented-window sink.
  // FR-STARTUP-003 T1: reuse the single-owner render-thread resources —
  // `g->renderer` constructed once from the DeviceCaps{1,1} baseline (the
  // same Vulkan 1.1 probe baseline every hermetic consumer uses; real
  // device caps arrive via the Vulkan probe lane, which this CPU window
  // mirror predates) and `g->staged` resized only on geometry change.
  // Geometry invalid -> skip the frame, keep the last good frame: the
  // locked bits stay untouched and unlockAndPost reposts the previous
  // frame instead of a torn copy.
  if (w <= 0 || !EnsureFrameResourcesLocked(g, stride, h)) {
    ANativeWindow_unlockAndPost(win);
    // T3: geometry drop still emits live markers so the proof pair stays
    // visible on every locked present; bits were never copied (last good
    // frame reposted above).
    EmitGalleryMarkersLocked(g, pages, w, h);
    return;
  }
  // T11 fix 2 (clear-then-post ordering): present into a staged buffer
  // first and copy into the locked window only on success, so a failed
  // present never posts the pre-cleared white buffer — the locked bits
  // stay untouched (previous frame reposted) instead.
  WindowSwapchain window(g->staged.data(), stride, w, h);
  // T3a: page wash follows the same-activity theme override (light surface
  // vs dark surface) so no black gutters leak through either mode.
  window.clear(ThemeInk(WindowRGBA(0xFE, 0xF7, 0xFF, 0xFF)));
  std::string error = g->parseError;
  // BUG-INPUT-001 T4: page change resets the dismiss stack to full so
  // away/back re-entry restarts from 5; the live count feeds the paint path
  // so dismiss and paint cannot diverge. Runs under mu (caller holds it).
  if (g->lastPresentedPage != g->page) {
    g->layeringOverlayCount = 5;
    g->lastPresentedPage = g->page;
  }
  const bool presented = ::runtime::PresentDeviceGalleryPage(
      g->rows, g->page, *g->renderer, window, h, &error,
      g->layeringOverlayCount);
  // FR-GRAPHICS-014 T2: requested-vs-effective present-path selection with
  // probe downgrade to the proven CPU push (AC-GRAPHICS-040/041). The
  // requested renderer defaults to `cpu`; the resolve gate maps it against
  // EGL/Skia availability into the effective frame path (never blank) and
  // names every downgrade `renderer-downgrade:<lane>-unavailable`. `egl`
  // dispatches to the FR-012 blit leg, `skia` to the FR-003/AC-039 Skia leg,
  // `cpu`/downgrade takes the proven CPU `SurfaceView` push below (the
  // staged->locked copy); no EGL/Skia failure can blank or crash the frame
  // per AC-GRAPHICS-034, and every downgrade is logged with requested vs
  // effective reported separately.
  // Stub scope (adjudicated MAJOR 1): `requestedRenderer`, `eglAvailable`,
  // and `skiaAvailable` below are stubbed defaults — no native preference
  // source exists in-tree (CLI-side per FR-WORKFLOW-006; device probing per
  // Q-GRAPHICS-006 is unsettled, and plumbing it in is out-of-scope new
  // interface work, so no preference source is invented here). The egl/skia
  // dispatch below is therefore armed-but-unreached until preference
  // plumbing lands (follow-up: wire the requested renderer + availability
  // probes into this gate); until then every frame takes the proven CPU
  // push with the observational EGL attempt preserved.
  // FR-GRAPHICS-012 T3: EGL blit-present attempt with automatic in-process
  // CPU fallback. Observational until T4 proves EGL pixels: the proven CPU
  // copy below always posts, so a failed (or dormant — EGL never initialized
  // yet) EGL lane can never blank the device frame. Every EGL failure is
  // logged inside as `GFX-BLIT-FALLBACK: <trigger>`; lock/unlockAndPost below
  // are reused unchanged.
  const char* requestedRenderer = "cpu";
  const bool eglAvailable = false;
  const bool skiaAvailable = false;
  char effectiveRenderer[16] = {};
  std::string rendererTrigger;
  ResolveRendererFallback(requestedRenderer, eglAvailable, skiaAvailable,
                          effectiveRenderer, sizeof(effectiveRenderer),
                          &rendererTrigger);
  if (!rendererTrigger.empty()) {
    const char* reqName =
        (requestedRenderer != nullptr) ? requestedRenderer : "cpu";
    std::string downgradeLog =
        rendererTrigger + " (requested=" + reqName +
        " effective=" + effectiveRenderer + ")";
    MuseLogInfo(downgradeLog);
  }
  if (presented && w > 0 && h > 0 && !g->staged.empty()) {
    if (std::strcmp(effectiveRenderer, "egl") == 0) {
      std::string eglFallback;
      PresentFrameWithFallback(g->staged.data(), w, h, &eglFallback);
      if (!eglFallback.empty()) {
        MuseLogInfo(eglFallback);
      }
    } else if (std::strcmp(effectiveRenderer, "skia") == 0) {
      std::string skiaFallback;
      PresentSkiaFrameWithFallback(g->staged.data(), w, h, &skiaFallback);
      if (!skiaFallback.empty()) {
        MuseLogInfo(skiaFallback);
      }
    } else {
      // FR-GRAPHICS-012 T3 observational leg restored (review MAJOR 3):
      // the base code attempted `PresentFrameWithFallback` unconditionally
      // when presented; the effective-`cpu` path preserves that attempt
      // here. Observational only — the proven CPU staged->locked copy below
      // still posts the frame, so a failed (or dormant) EGL lane can never
      // blank it. A non-empty EGL trigger surfaces via MuseLogInfo (review
      // MINOR: captured triggers were discarded).
      std::string eglFallback;
      PresentFrameWithFallback(g->staged.data(), w, h, &eglFallback);
      if (!eglFallback.empty()) {
        MuseLogInfo(eglFallback);
      }
    }
  }
  // T3: staged->locked copy gated on presented — when false the locked bits
  // stay untouched and the unlockAndPost below reposts the previous frame.
  if (presented) {
    std::uint32_t* bits = static_cast<std::uint32_t*>(buf.bits);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        bits[y * stride + x] = g->staged[static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(stride) +
                                      static_cast<std::size_t>(x)];
      }
    }
  }
  const int drawn = presented ? static_cast<int>(g->rows.size()) : 0;
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

// FR-STARTUP-003 T2 (AC-STARTUP-005, RULE-STARTUP-002): single-owner render
// thread + single-pending-frame queue. All ANativeWindow lock/raster runs on
// the one render_thread via RenderLocked; UI-thread JNI entries only enqueue
// (EnqueueFrame) under the short qmu critical section and return immediately,
// never waiting on ANativeWindow_lock/raster. Rapid calls coalesce: at most
// one pendingFrame is ever queued (no backlog, no unbounded growth). The
// queued work holds a counted ToGallery ref (shared_ptr captured at enqueue
// time via weak_ptr promotion per wakeup), and teardown (detach/destroy)
// drains/cancels the pending frame before releasing the window, so the thread
// never draws unowned (RULE-RUNTIME-004). Exact drain-vs-cancel primitive:
// flag + queue clear under mu/qmu. Never throws across JNI.
void RenderThreadMain(std::weak_ptr<GalleryHandle> weak) {
  try {
    while (true) {
      std::shared_ptr<GalleryHandle> g = weak.lock();
      if (g == nullptr) return;
      // Wait for work or shutdown under the queue lock only (never holds
      // g->mu here, so the UI-thread enqueue critical section stays short).
      std::unique_lock<std::mutex> ql(g->qmu);
      g->qcv.wait(ql, [&] { return g->pendingFrame || g->renderShutdown; });
      if (g->renderShutdown) {
        g->pendingFrame = false;
        return;
      }
      // Consume the single pending slot (coalesce point: N rapid enqueues
      // collapse to one extra present).
      g->pendingFrame = false;
      ql.unlock();
      // Re-promote under the render thread: teardown (ClaimForDestroy) may
      // have unregistered the handle; a null promotion means destroyed.
      std::shared_ptr<GalleryHandle> live = weak.lock();
      if (live == nullptr) return;
      {
        std::lock_guard<std::mutex> lock(live->mu);
        // Shutdown/detach may have landed between wakeup and here: re-check
        // under mu and skip the draw (drain, keep last good frame).
        bool cancelled = false;
        {
          std::lock_guard<std::mutex> ql2(live->qmu);
          cancelled = live->renderShutdown;
        }
        if (cancelled) return;
        RenderLocked(live.get());
      }
    }
  } catch (...) {
    // Render thread never propagates: frame skipped, thread exits quietly.
  }
}

// Starts the single render thread once per handle. Caller holds no locks;
// takes the counted ref so the thread's weak owner stays valid at spawn.
void EnsureRenderThread(const std::shared_ptr<GalleryHandle>& g) {
  if (g == nullptr) return;
  std::lock_guard<std::mutex> ql(g->qmu);
  if (g->renderStarted) return;
  g->renderStarted = true;
  g->renderShutdown = false;
  std::weak_ptr<GalleryHandle> weak = g;
  try {
    g->render_thread = std::thread([weak] { RenderThreadMain(weak); });
  } catch (...) {
    g->renderStarted = false;
  }
}

// EnqueueFrame: the only UI-thread frame path. Sets at most one pendingFrame
// (coalesce) and wakes the render_thread; returns without any
// ANativeWindow_lock/raster. Never blocks on a frame in flight.
void EnqueueFrame(const std::shared_ptr<GalleryHandle>& g) {
  if (g == nullptr) return;
  EnsureRenderThread(g);
  {
    std::lock_guard<std::mutex> ql(g->qmu);
    if (g->renderShutdown) return;
    g->pendingFrame = true;  // coalesce: overwrite, never append.
  }
  g->qcv.notify_one();
}

// Teardown drain (RULE-RUNTIME-004): cancel the pending frame so a racing
// queued frame never draws after detach/destroy. Window release still happens
// under g->mu by the caller.
void CancelPendingFrame(const std::shared_ptr<GalleryHandle>& g) {
  if (g == nullptr) return;
  {
    std::lock_guard<std::mutex> ql(g->qmu);
    g->pendingFrame = false;
  }
}

// Exclusive teardown stop: drain the queue, signal shutdown, join the single
// render thread. Caller must NOT hold g->mu (the render thread locks g->mu
// per frame); caller holds the ClaimForDestroy ref so the join is safe.
void ShutdownRenderThread(const std::shared_ptr<GalleryHandle>& g) {
  if (g == nullptr) return;
  {
    std::lock_guard<std::mutex> ql(g->qmu);
    g->renderShutdown = true;
    g->pendingFrame = false;
  }
  g->qcv.notify_all();
  try {
    if (g->render_thread.joinable()) {
      g->render_thread.join();
    }
  } catch (...) {
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
    std::shared_ptr<GalleryHandle> g = std::make_shared<GalleryHandle>();
    g->tree.resize(static_cast<std::size_t>(len));
    env->GetByteArrayRegion(treeBytes, 0, len,
                            reinterpret_cast<jbyte*>(g->tree.data()));
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
      // g is a counted ref: returning drops it, no explicit delete.
      return 0;
    }
    g->rows = ::runtime::ParseGalleryRows(g->tree.data(), g->tree.size(),
                                          &g->parseError);
    g->parsed = true;
    // Review BLOCKER: populate the tap tree from the laid-out bounds at
    // load so the device dispatch path resolves real button nodes. Runs
    // before registration (single-threaded here: no entry can observe the
    // handle yet, so no lock is needed).
    PopulateTapTreeFromLayout(g->tapTree);
    RegisterHandle(g);
#ifdef __ANDROID__
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "nativeLoadTree: bytes=%d rows=%d%s%s -> handle acquired",
                  static_cast<int>(len), static_cast<int>(g->rows.size()),
                  g->parseError.empty() ? "" : " parseWarn=",
                  g->parseError.empty() ? "" : g->parseError.c_str());
    MuseLogInfo(msg);
    {
      char prov[192];
      std::snprintf(prov, sizeof(prov),
                    "nativeProvenance: engine-sha=%s gallery_present-sha=%s",
                    ENGINE_GIT_SHA, GALLERY_PRESENT_SHA);
      MuseLogInfo(prov);
    }
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
    return static_cast<jlong>(reinterpret_cast<intptr_t>(g.get()));
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
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
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
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    // FR-STARTUP-003 T2: enqueue-and-return — no ANativeWindow_lock/raster on
    // the calling (UI) thread. RenderLocked runs only on the single render
    // thread; rapid calls coalesce to at most one pending frame.
    EnqueueFrame(g);
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
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) return 0;
  try {
#ifdef __ANDROID__
    int pageOut = 0;
    {
      std::lock_guard<std::mutex> lock(g->mu);
      g->page += 1;
      pageOut = g->page;
    }
    // FR-STARTUP-003 T2: state mutation above returns immediately; the frame
    // itself posts to the same single-owner queue (no inline RenderLocked).
    EnqueueFrame(g);
    {
      // A page turn is a deliberate user action: always worth one line.
      char msg[64];
      std::snprintf(msg, sizeof(msg), "nativeAdvancePage: page %d",
                    pageOut + 1);
      MuseLogInfo(msg);
    }
    return static_cast<jint>(pageOut);
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

// T3a (BUG-M3-FIDELITY-002 / AC-M3-010): same-activity theme switch entry.
// Java: com.muse.host.MainActivity.nativeSetTheme(long, int). Flips the
// process-wide palette override in place on the same GalleryHandle/page and
// re-renders through the existing RenderLocked path — no new activity, no
// restart, state/scroll preserved (page index untouched).
// Fail-closed mirror of nativeAdvancePage: null env, pending exception, an
// unregistered (zero, forged, stale) handle, or a nightMode outside {0, 1}
// yields -1 with no exception crossing into the VM and no dereference.
// Returns measured repaint frames from the CLOCK_MONOTONIC clock over the
// 16.6667ms nominal vsync interval (min 1, never a constant); the frame-timing
// line below matches the T2 gate FRAME_TIMING_RE (`framesMeasured`).
extern "C" MUSE_JNI_EXPORT jint Java_com_muse_host_MainActivity_nativeSetTheme(
    JNIEnv* env, jobject thiz, jlong handle, jint nightMode) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return -1;
  }
  if (nightMode != 0 && nightMode != 1) {
    return -1;
  }
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) return -1;
  try {
#ifdef __ANDROID__
    struct timespec t0;
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int page1 = 0;
    {
      std::lock_guard<std::mutex> lock(g->mu);
      g_nightMode.store(nightMode != 0 ? 1 : 0, std::memory_order_relaxed);
      page1 = g->page + 1;
    }
    // FR-STARTUP-003 T2: no inline RenderLocked on the calling thread — the
    // repaint posts to the same single-owner queue and returns immediately.
    EnqueueFrame(g);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double elapsedMs =
        static_cast<double>(t1.tv_sec - t0.tv_sec) * 1000.0 +
        static_cast<double>(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    int frames = static_cast<int>(elapsedMs / 16.6667 + 0.5);
    if (frames < 1) frames = 1;
    {
      char msg[224];
      std::snprintf(msg, sizeof(msg),
                    "nativeSetTheme: mode=%s framesMeasured=%d "
                    "frameTimeMs=%.3f clock=clock_gettime-CLOCK_MONOTONIC "
                    "vsyncMs=16.6667 page=%d",
                    nightMode != 0 ? "dark" : "light", frames, elapsedMs,
                    page1);
      MuseLogInfo(msg);
    }
    {
      char marker[128];
      std::snprintf(marker, sizeof(marker),
                    "gallery-theme=%s gallery-page=%d framesMeasured=%d",
                    nightMode != 0 ? "dark" : "light", page1, frames);
      GalleryLogInfo(marker);
    }
    return static_cast<jint>(frames);
#else
    (void)g;
    return 1;
#endif
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return -1;
  }
}

// JNI touch entry (BUG-M3-FIDELITY-007 T3/T5; FR-INPUT-011 / AC-INPUT-020).
// Java forwards raw MotionEvent fields (action, x, y, pointerId,
// timestampMs); node-id resolution happens natively from laid-out bounds —
// never a pre-decided node id from Java (plan A1). Fail-closed mirror of the
// sibling entries: null env, pending exception, or an unregistered (zero,
// forged, stale) handle yields -1 with no exception crossing into the VM
// and no dereference. Coordinates cross raw and are only trusted once the
// env/handle guards pass; ResolveTapTarget fails negative/NaN closed.
// Valid-env dispatch core (T5): ACTION_DOWN records the down origin and adds
// the pointer; ACTION_UP resolves the target from laid-out bounds, claims
// via ClaimTap, and fires exactly once via FireTapHandler (ClaimTap-
// exclusive: no direct arena read bypasses the claim). A fired tap posts
// its write-through repaint hook and re-renders on the gallery path; the
// one-frame vsync proof itself is device-measured (T7).
// Returns the claimed-and-fired node id, or -1 when nothing fired.
extern "C" MUSE_JNI_EXPORT jint Java_com_muse_host_MainActivity_nativeDispatchTouch(
    JNIEnv* env, jobject thiz, jlong handle, jint action, jfloat x, jfloat y,
    jint pointerId, jlong timestampMs) {
  (void)thiz;
  (void)timestampMs;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return -1;
  }
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) {
    return -1;
  }
  try {
    // MotionEvent action codes (platform constants, mirrored hermetically
    // in jni_touch_dispatch_shape_test.cpp).
    constexpr jint kActionDown = 0;
    constexpr jint kActionUp = 1;
    input::AxisState axes{};
    axes.x = x;
    axes.y = y;
    axes.pressure = 1.0f;
    const input::NormalizedEvent ev = input::NormalizeMotion(
        pointerId, input::ToolType::Touch, 0u, axes, 0u,
        input::SourceClass::Touch);
    // FR-STARTUP-003 T2: gather tap verdict under g->mu, then enqueue the
    // repaint outside the mu critical section (no inline RenderLocked on the
    // calling thread; the single render thread locks mu per frame).
    int firedTarget = -1;
    bool firedPosted = false;
    {
      std::lock_guard<std::mutex> lock(g->mu);
      if (action == kActionDown) {
#ifdef __ANDROID__
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "nativeDispatchTouch: action=%d x=%.1f y=%.1f pointer=%d",
                      static_cast<int>(action), static_cast<double>(x),
                      static_cast<double>(y), static_cast<int>(pointerId));
        MuseLogInfo(msg);
#endif
        RecordTapDown(pointerId, x, y);
        g->tapArena.AddPointer(ev, g->tapTree);
        return -1;
      }
      if (action == kActionUp) {
        const int32_t target = ResolveTapTarget(g->tapTree, x, y);
        if (target < 0) {
          return -1;
        }
        const ArenaAction claim = ClaimTap(g->tapArena, pointerId, target);
        if (!claim.claimed) {
          return -1;
        }
        if (!FireTapHandler(target)) {
          return -1;
        }
        // BUG-INPUT-001 T3 (AC-M3-022 scrim clause): layering-page scrim-tap
        // routing. scrimHit rides the T2 scrim hit-target (node 20, child of
        // root 10 with content 21 above it, so button taps never read as
        // scrim). isDrag is false here by construction: a past-slop drag
        // fails the ClaimTap slop verdict above (unclaimed → early -1) before
        // any fire. A topmost dismiss applies the single pop here under mu;
        // the existing EnqueueFrame below repaints outside the lock.
        // Non-layering pages never route (present-path predicate below).
        const bool scrimHit = (target == 20);
        if (scrimHit && g->lastViewportH > 0 && !g->rows.empty()) {
          bool onLayeringPage = false;
          const std::vector<int> starts =
              ::runtime::PageStarts(g->rows, g->lastViewportH);
          const int pages = static_cast<int>(starts.size()) - 1;
          int cur = g->page;
          if (pages > 0) {
            cur %= pages;
            if (cur < 0) cur += pages;
          }
          if (pages > 0 && cur >= 0 && cur < pages) {
            onLayeringPage = ::runtime::IsLayeringPageSlice(
                g->rows, starts[static_cast<std::size_t>(cur)],
                starts[static_cast<std::size_t>(cur) + 1] - 1);
          }
          if (onLayeringPage) {
            const ::runtime::DismissResult dismiss = ::runtime::onScrimTap(
                true, /*isDrag=*/false, g->layeringOverlayCount);
            if (dismiss.dismissed == "topmost") {
              g->layeringOverlayCount = dismiss.remaining;
            }
          }
        }
        const bool posted = RepaintWithinOneFrame(target);
        firedTarget = static_cast<int>(target);
        firedPosted = posted;
      } else {
        // MOVE and other actions: keep the arena displacement current so the
        // UP slop verdict measures the full gesture; never fires.
        g->tapArena.AddPointer(ev, g->tapTree);
        return -1;
      }
    }
    if (action == kActionUp && firedTarget >= 0) {
#ifdef __ANDROID__
      // Same single-owner queue as nativeRenderFrame (coalesce, not backlog).
      EnqueueFrame(g);
      {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "nativeDispatchTouch: tap fired node=%d repaint=%d",
                      firedTarget, firedPosted ? 1 : 0);
        MuseLogInfo(msg);
      }
#endif
      return static_cast<jint>(firedTarget);
    }
    return -1;
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
    return -1;
  }
}

// JNI IME inset feed (BUG-RUNTIME-004 T3 / FR-LAYOUT-012 typed inset bus).
// Java: com.muse.host.MainActivity.nativeSetImeBottom(long, float), fed per
// animation frame by onInsetsFrame(imeBottomPx). Forwards the frame height
// into runtime::SetPresentImeBottom so PresentDeps().insets.imeBottom (the
// same px field the T1 anchor consumes via the T4 call site) follows the
// keyboard within one frame. Fail-closed mirror of the sibling entries:
// null env, pending exception, or an unregistered (zero, forged, stale)
// handle yields a no-op with no exception crossing into the VM and no
// dereference. Clamping (non-finite/negative -> 0) lives in
// SetPresentImeBottom; this entry never throws.
extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeSetImeBottom(
    JNIEnv* env, jclass clazz, jlong handle, jfloat imeBottomPx) {
  (void)clazz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) return;
  try {
    ::runtime::SetPresentImeBottom(static_cast<float>(imeBottomPx));
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_MainActivity_nativeDetachSurface(
    JNIEnv* env, jobject thiz, jlong handle) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
  if (g == nullptr) return;
  try {
#ifdef __ANDROID__
    // FR-STARTUP-003 T2 teardown drain (RULE-RUNTIME-004): cancel the pending
    // frame first so a racing queued frame never draws after detach, then
    // release the window and null it under g->mu.
    CancelPendingFrame(g);
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
  // Atomic, exclusive teardown: a single registry-locked claim hands the
  // live handle out exactly once (re-entry during destroy, stale-slot reuse
  // after destroy, and racing destroys all fail closed in ClaimForDestroy
  // with no deref). New dispatches fail closed too (erased from the live
  // set). The teardown lock below waits on g->mu, so an in-flight dispatch
  // — which holds its own counted ref across its critical section — finishes
  // first; the object itself dies only when that last in-flight ref drains
  // (the local `claimed` going out of scope here). The Java side zeroes its
  // slot after this call (engineHandle = 0).
  std::shared_ptr<GalleryHandle> claimed = ClaimForDestroy(handle);
  if (claimed == nullptr) return;
  try {
#ifdef __ANDROID__
    // FR-STARTUP-003 T2 exclusive teardown (RULE-RUNTIME-004): stop the single
    // render thread and drain/cancel the pending frame BEFORE releasing the
    // window, so no queued frame ever draws unowned. ShutdownRenderThread
    // joins without holding mu (the render thread locks mu per frame).
    ShutdownRenderThread(claimed);
    std::lock_guard<std::mutex> lock(claimed->mu);
    if (claimed->window != nullptr) {
      ANativeWindow_release(claimed->window);
      claimed->window = nullptr;
    }
#endif
  } catch (...) {
  }
  // No explicit delete: `claimed` going out of scope drops the destroy-side
  // ref; in-flight holders keep the handle alive until they release g->mu.
}

// T8 (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-011/012/014): in-app
// IME/text/clipboard observation surface — logcat-relay sink.
//
// The Java probe field (`IMEInterceptingEditText`) forwards every IME,
// TextWatcher, and clipboard event through `nativeOnIMEEvent` /
// `nativeOnTextUpdated` (exact contract signatures). This sink does no heavy
// work in the callback: it copies the strings, canonicalizes the event id,
// and emits exactly one `muse_text` JSON logcat line per call
// (`adb logcat -s muse_text`), then returns. Fail-closed/never-throw:
// exception-check + early return on every JNI entry; null/empty/rapid
// inputs never crash the process.
//
// Canonical event ids: the sink owns one atomic sequence shared by both
// entries. Each line takes `max(javaHint, seq + 1)` via CAS, so the emitted
// `eventId` stream is strictly monotonic with continuity == count even when
// the TextWatcher entry (which carries no id) interleaves with IME events
// or callbacks race. A separate atomic receive-counter is emitted as `rx`
// per line for the tester's same-stream count proof.
//
// Envelope: span-carrying and clipboard events pack trailing metadata into
// the `text` argument as `<payload>\x01key=value;...` (keys: spanStart,
// spanEnd, mime). The sink strips the envelope and re-emits those keys
// top-level; a malformed envelope fails closed (whole string stays `text`).
namespace {

constexpr char kMuseTextTag[] = "muse_text";

// Canonical event-id sequence + receive counter (relaxed: ordering across
// lines is established by the CAS/id itself, not by cross-thread sync).
std::atomic<long long> g_museTextSeq{0};
std::atomic<long long> g_museTextRx{0};

// Strictly parses the trailing `\x01` envelope. On success sets the outputs
// (span pair only when BOTH parse with 0 <= start <= end; mime only when it
// matches the token vocabulary) and returns the payload end (index of the
// separator). On any malformation returns npos: the whole string is payload.
std::size_t SplitMuseTextEnvelope(const std::string& text, long long* spanStart,
                                   long long* spanEnd, std::string* mime) {
  const std::size_t sep = text.find_last_of('\x01');
  if (sep == std::string::npos || sep + 1 >= text.size()) {
    return std::string::npos;
  }
  const std::string tail = text.substr(sep + 1);
  if (tail.empty()) {
    return std::string::npos;
  }
  long long start = -1;
  long long end = -1;
  bool sawSpanStart = false;
  bool sawSpanEnd = false;
  std::string mimeValue;
  bool sawMime = false;
  std::size_t pos = 0;
  while (pos <= tail.size()) {
    const std::size_t semi = tail.find(';', pos);
    const std::string kv =
        (semi == std::string::npos) ? tail.substr(pos) : tail.substr(pos, semi - pos);
    const std::size_t eq = kv.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= kv.size()) {
      return std::string::npos;
    }
    const std::string key = kv.substr(0, eq);
    const std::string value = kv.substr(eq + 1);
    if (key == "spanStart" || key == "spanEnd") {
      if (value.size() > 9) {
        return std::string::npos;
      }
      for (char c : value) {
        if (c < '0' || c > '9') {
          return std::string::npos;
        }
      }
      long long parsed = 0;
      for (char c : value) {
        parsed = parsed * 10 + (c - '0');
      }
      if (key == "spanStart") {
        if (sawSpanStart) {
          return std::string::npos;
        }
        sawSpanStart = true;
        start = parsed;
      } else {
        if (sawSpanEnd) {
          return std::string::npos;
        }
        sawSpanEnd = true;
        end = parsed;
      }
    } else if (key == "mime") {
      if (sawMime || value.empty() || value.size() > 64) {
        return std::string::npos;
      }
      for (char c : value) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '+' ||
                        c == '-' || c == '*' || c == '/';
        if (!ok) {
          return std::string::npos;
        }
      }
      sawMime = true;
      mimeValue = value;
    } else {
      return std::string::npos;
    }
    if (semi == std::string::npos) {
      break;
    }
    pos = semi + 1;
  }
  if (sawSpanStart != sawSpanEnd) {
    return std::string::npos;
  }
  if (sawSpanStart && !(start >= 0 && end >= start)) {
    return std::string::npos;
  }
  if (sawSpanStart) {
    *spanStart = start;
    *spanEnd = end;
  }
  if (sawMime) {
    *mime = mimeValue;
  }
  return sep;
}

// JSON string escaper for the one-line relay: quotes, backslashes, and
// control bytes are escaped; UTF-8 continuation bytes pass through so the
// line stays single-line and parseable. Never throws (works on raw bytes).
void AppendMuseTextJsonString(std::string* out, const char* data, std::size_t len) {
  static const char* kHex = "0123456789abcdef";
  out->push_back('"');
  for (std::size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\b':
        out->append("\\b");
        break;
      case '\f':
        out->append("\\f");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (c < 0x20) {
          out->append("\\u00");
          out->push_back(kHex[(c >> 4) & 0xF]);
          out->push_back(kHex[c & 0xF]);
        } else {
          out->push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out->push_back('"');
}

// Publishes one canonical event id: max(hint, seq + 1) via CAS (hint <= 0
// always advances). Returns the id stamped on the emitted line.
long long PublishMuseTextId(long long hint) {
  long long cur = g_museTextSeq.load(std::memory_order_relaxed);
  for (;;) {
    long long want = (hint > cur) ? hint : (cur + 1);
    if (want <= 0) {
      want = cur + 1;
    }
    if (g_museTextSeq.compare_exchange_weak(cur, want, std::memory_order_relaxed)) {
      return want;
    }
  }
}

}  // namespace

// Relay definitions (declared in `runtime/gallery_present.h`). Noexcept in
// practice: every failure mode is absorbed into a degraded-but-valid line.
void EmitMuseTextEvent(const char* eventType, const char* text,
                         int cursorPosition, long long eventId) {
  try {
    const std::string type = eventType != nullptr ? eventType : "";
    const std::string payload = text != nullptr ? text : "";
    const long long id = PublishMuseTextId(eventId);
    const long long rx = g_museTextRx.fetch_add(1, std::memory_order_relaxed) + 1;
    // Strip the optional trailing envelope; malformed envelopes stay payload.
    long long spanStart = -1;
    long long spanEnd = -1;
    std::string mime;
    std::size_t payloadLen = payload.size();
    const std::size_t sep =
        SplitMuseTextEnvelope(payload, &spanStart, &spanEnd, &mime);
    if (sep != std::string::npos) {
      payloadLen = sep;
    }
    std::string line;
    line.reserve(payloadLen + type.size() + mime.size() + 128);
    line.append("{\"eventId\":");
    line.append(std::to_string(id));
    line.append(",\"rx\":");
    line.append(std::to_string(rx));
    line.append(",\"type\":");
    AppendMuseTextJsonString(&line, type.data(), type.size());
    line.append(",\"text\":");
    AppendMuseTextJsonString(&line, payload.data(), payloadLen);
    line.append(",\"cursor\":");
    line.append(std::to_string(cursorPosition < 0 ? 0 : cursorPosition));
    if (sep != std::string::npos && spanStart >= 0 && spanEnd >= 0) {
      line.append(",\"spanStart\":");
      line.append(std::to_string(spanStart));
      line.append(",\"spanEnd\":");
      line.append(std::to_string(spanEnd));
    }
    if (sep != std::string::npos && !mime.empty()) {
      line.append(",\"mime\":");
      AppendMuseTextJsonString(&line, mime.data(), mime.size());
    }
    line.push_back('}');
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_DEBUG, kMuseTextTag, "%s", line.c_str());
#else
    (void)kMuseTextTag;
#endif
  } catch (...) {
    // Fail-closed: the sink never throws.
  }
}

void EmitMuseTextUpdated(const char* currentText) {
  try {
    const std::string payload = currentText != nullptr ? currentText : "";
    const long long id = PublishMuseTextId(0);
    const long long rx = g_museTextRx.fetch_add(1, std::memory_order_relaxed) + 1;
    std::string line;
    line.reserve(payload.size() + 96);
    line.append("{\"eventId\":");
    line.append(std::to_string(id));
    line.append(",\"rx\":");
    line.append(std::to_string(rx));
    line.append(",\"type\":\"text_updated\",\"text\":");
    AppendMuseTextJsonString(&line, payload.data(), payload.size());
    line.append(",\"cursor\":-1}");
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_DEBUG, kMuseTextTag, "%s", line.c_str());
#else
    (void)kMuseTextTag;
#endif
  } catch (...) {
    // Fail-closed: the sink never throws.
  }
}

long long MuseTextReceiveCount() noexcept {
  try {
    return g_museTextRx.load(std::memory_order_relaxed);
  } catch (...) {
    return 0;
  }
}

// JNI observation entries for `com.muse.host.IMEInterceptingEditText`
// (exact T8 contract signatures). Fail-closed mirror of the sibling
// entries: null env, a pending exception, or any C++/JNI failure yields an
// early return with no exception crossing into the VM. Strings are copied
// out (length-bounded, Modified-UTF-8 safe) and released before the relay
// runs, so no JNI reference is held across the log call.
extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_IMEInterceptingEditText_nativeOnIMEEvent(
    JNIEnv* env, jobject thiz, jstring eventType, jstring text,
    jint cursorPosition, jlong eventId) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  try {
    std::string typeCopy;
    std::string textCopy;
    if (eventType != nullptr) {
      const jsize ulen = env->GetStringUTFLength(eventType);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      const char* chars = env->GetStringUTFChars(eventType, nullptr);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      if (chars != nullptr) {
        typeCopy.assign(chars, ulen >= 0 ? static_cast<std::size_t>(ulen) : 0);
        env->ReleaseStringUTFChars(eventType, chars);
      }
    }
    if (text != nullptr) {
      const jsize ulen = env->GetStringUTFLength(text);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      const char* chars = env->GetStringUTFChars(text, nullptr);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      if (chars != nullptr) {
        textCopy.assign(chars, ulen >= 0 ? static_cast<std::size_t>(ulen) : 0);
        env->ReleaseStringUTFChars(text, chars);
      }
    }
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
      return;
    }
    ::EmitMuseTextEvent(typeCopy.c_str(), textCopy.c_str(),
                        static_cast<int>(cursorPosition),
                        static_cast<long long>(eventId));
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}

extern "C" MUSE_JNI_EXPORT void Java_com_muse_host_IMEInterceptingEditText_nativeOnTextUpdated(
    JNIEnv* env, jobject thiz, jstring currentText) {
  (void)thiz;
  if (env == nullptr || env->ExceptionCheck() == JNI_TRUE) {
    return;
  }
  try {
    std::string textCopy;
    if (currentText != nullptr) {
      const jsize ulen = env->GetStringUTFLength(currentText);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      const char* chars = env->GetStringUTFChars(currentText, nullptr);
      if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        return;
      }
      if (chars != nullptr) {
        textCopy.assign(chars, ulen >= 0 ? static_cast<std::size_t>(ulen) : 0);
        env->ReleaseStringUTFChars(currentText, chars);
      }
    }
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
      return;
    }
    ::EmitMuseTextUpdated(textCopy.c_str());
  } catch (...) {
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionClear();
    }
  }
}
