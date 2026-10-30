// JNI boundary implementation (NFR-RUNTIME-001 / AC-RUNTIME-025).
// This is the only translation unit that knows the engine-backed host type;
// the public header above exposes opaque void* handles plus the RuntimeHost
// ABC, so no engine type reaches JNI consumers.

#include "runtime/jni_bridge.h"
#include "runtime/presenter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
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
  // Device tap chain state (BUG-M3-FIDELITY-007 T5): layout-bounds hit-test
  // tree plus the gesture arena the valid-env dispatch body drives. The
  // tree is populated from the layout engine's laid-out bounds; until then
  // it is empty and every tap resolves -1 (fail closed, never a phantom
  // node). Guarded by mu like the rest of the handle.
  HitTestTree tapTree;
  GestureArena tapArena;
#ifdef __ANDROID__
  ANativeWindow* window = nullptr;
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
      const std::uint32_t ink = cmd.payload.textColor != 0
                                    ? cmd.payload.textColor
                                    : WindowRGBA(0x1D, 0x1B, 0x20, 0xFF);
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
    switch (cmd.kind) {
      case PrimitiveKind::Text:
      case PrimitiveKind::ShapedText:
        return cmd.payload.textColor != 0
                   ? cmd.payload.textColor
                   : WindowRGBA(0x1D, 0x1B, 0x20, 0xFF);
      case PrimitiveKind::StateLayer:
        return cmd.payload.stateColor != 0
                   ? cmd.payload.stateColor
                   : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
      case PrimitiveKind::Ripple:
        return cmd.payload.rippleColor != 0
                   ? cmd.payload.rippleColor
                   : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
      case PrimitiveKind::VectorIcon:
        return cmd.payload.tintColor != 0
                   ? cmd.payload.tintColor
                   : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
      case PrimitiveKind::CircleArc:
        return cmd.payload.arcColor != 0
                   ? cmd.payload.arcColor
                   : WindowRGBA(0x67, 0x50, 0xA4, 0xFF);
      case PrimitiveKind::OverlayScrim: {
        const float alpha = cmd.payload.scrimAlpha;
        const unsigned a = (alpha <= 0.0f)
                               ? 0u
                               : (alpha >= 1.0f)
                                     ? 255u
                                     : static_cast<unsigned>(255.0f * alpha +
                                                             0.5f);
        const std::uint32_t c = cmd.payload.scrimColor;
        return (c & 0x00FFFFFFu) |
               (static_cast<std::uint32_t>(a & 0xFFu) << 24);
      }
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
        // T11 fix 3: card/band fills. A nonzero payload selects the
        // emitted fill; payload-0 selects the real default tonal card
        // fill (M3 surface-variant), never background-on-background.
        if (cmd.payload.stateColor != 0) return cmd.payload.stateColor;
        return WindowRGBA(0xE7, 0xE0, 0xEC, 0xFF);
      default:
        return WindowRGBA(0xF7, 0xF2, 0xFA, 0xFF);
    }
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
    const std::uint32_t c = cmd.payload.shadowColor;
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
  // F1 sink fix: the presented window is driven by the retained Swapchain
  // text output, not the CPU oracle. Present the device gallery page
  // through Renderer draw into the window-attached Swapchain; the CPU
  // RasterGalleryPage oracle is no longer the presented-window sink.
  // Baseline caps match the Vulkan 1.1 probe every hermetic consumer uses;
  // real device caps arrive via the Vulkan probe lane, which this CPU
  // window mirror predates.
  ::runtime::DeviceCaps baselineCaps;
  baselineCaps.apiMajor = 1;
  baselineCaps.apiMinor = 1;
  ::runtime::Renderer renderer(baselineCaps);
  // T11 fix 2 (clear-then-post ordering): present into a staged buffer
  // first and copy into the locked window only on success, so a failed
  // present never posts the pre-cleared white buffer — the locked bits
  // stay untouched (previous frame reposted) instead.
  std::vector<std::uint32_t> staged(
      static_cast<std::size_t>(stride) * static_cast<std::size_t>(h), 0u);
  WindowSwapchain window(staged.data(), stride, w, h);
  window.clear(WindowRGBA(0xFE, 0xF7, 0xFF, 0xFF));
  std::string error = g->parseError;
  const bool presented = ::runtime::PresentDeviceGalleryPage(
      g->rows, g->page, renderer, window, h, &error);
  if (presented) {
    std::uint32_t* bits = static_cast<std::uint32_t*>(buf.bits);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        bits[y * stride + x] = staged[static_cast<std::size_t>(y) *
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
    std::lock_guard<std::mutex> lock(g->mu);
    RenderLocked(g.get());
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
    std::lock_guard<std::mutex> lock(g->mu);
    g->page += 1;
    RenderLocked(g.get());
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
      const bool posted = RepaintWithinOneFrame(target);
      (void)posted;
#ifdef __ANDROID__
      RenderLocked(g.get());
      {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "nativeDispatchTouch: tap fired node=%d repaint=%d",
                      static_cast<int>(target), posted ? 1 : 0);
        MuseLogInfo(msg);
      }
#endif
      return static_cast<jint>(target);
    }
    // MOVE and other actions: keep the arena displacement current so the
    // UP slop verdict measures the full gesture; never fires.
    g->tapArena.AddPointer(ev, g->tapTree);
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
  std::shared_ptr<GalleryHandle> g = ToGallery(handle);
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
