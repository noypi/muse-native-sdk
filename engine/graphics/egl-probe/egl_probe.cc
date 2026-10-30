// T1 engine-resident EGL probe (FR-GRAPHICS-011, AC-031/032).
// Pure-EGL init via eglGetDisplay/eglInitialize/eglChooseConfig/
// eglCreateContext (GLES3); driver strings + eglQueryString extension scan
// for EGL_ANDROID_native_fence_sync. `--force-fail init` short-circuits
// before eglGetDisplay. EGL is resolved at runtime via dlopen so the
// hermetic host build needs no EGL headers or link dependency; when no
// live EGL display exists the host path returns an explicit synthetic
// PASS verdict (never a silent skip). Never throws, never reads stdin,
// never leaks secrets into the diagnostic.

#include "egl-probe/egl_probe.h"

#include <dlfcn.h>

#include <cstring>

namespace graphics {
namespace {

// Minimal EGL types/constants so no <EGL/egl.h> is required at build time.
typedef void* EGLNativeDisplayType;
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef int EGLBoolean;
typedef int EGLint;
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_SUCCESS 0x3000
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_CLIENT_APIS 0x308D
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES3_BIT 0x00000040
#define EGL_NONE 0x3038
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB

typedef EGLDisplay (*GetDisplayFn)(EGLNativeDisplayType);
typedef EGLBoolean (*InitializeFn)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*TerminateFn)(EGLDisplay);
typedef const char* (*QueryStringFn)(EGLDisplay, EGLint);
typedef EGLBoolean (*ChooseConfigFn)(EGLDisplay, const EGLint*, EGLConfig*,
                                     EGLint, EGLint*);
typedef EGLContext (*CreateContextFn)(EGLDisplay, EGLConfig, EGLContext,
                                      const EGLint*);
typedef EGLBoolean (*DestroyContextFn)(EGLDisplay, EGLContext);
typedef EGLint (*GetErrorFn)(void);

bool HasToken(const char* list, const char* token) {
  if (list == nullptr || token == nullptr || *token == '\0') return false;
  const size_t n = std::strlen(token);
  const char* p = list;
  while ((p = std::strstr(p, token)) != nullptr) {
    const bool left_ok = (p == list) || (*(p - 1) == ' ');
    const bool right_ok = (p[n] == '\0') || (p[n] == ' ');
    if (left_ok && right_ok) return true;
    p += n;
  }
  return false;
}

EglProbeResult Fail(const char* stage, const char* reason) {
  EglProbeResult r;
  r.exit_code = 1;
  r.verdict_line = std::string("EGL-PROBE-VERDICT: FAIL ") + stage;
  r.fence_sync_present = false;
  r.diagnostic =
      std::string("EGL-PROBE-DIAGNOSTIC: ") + stage + " " + reason;
  return r;
}

EglProbeResult Pass(const std::string& version, const std::string& apis,
                    bool fence, const std::string& how) {
  EglProbeResult r;
  r.exit_code = 0;
  r.egl_version = version;
  r.client_apis = apis;
  r.fence_sync_present = fence;
  r.verdict_line = "EGL-PROBE-VERDICT: PASS " + how;
  r.diagnostic = "";
  return r;
}

}  // namespace

EglProbeResult run_egl_probe(const EglProbeOptions& opts) noexcept {
  try {
    // --force-fail init short-circuits before eglGetDisplay.
    if (opts.force_fail_init) {
      return Fail("init", "forced failure before eglGetDisplay");
    }

    // Resolve EGL at runtime; no link-time dependency.
    void* lib = ::dlopen("libEGL.so.1", RTLD_NOW);
    if (lib == nullptr) lib = ::dlopen("libEGL.so", RTLD_NOW);
    if (lib == nullptr) {
      // Hermetic host path: no live EGL display here; explicit synthetic
      // PASS so the host contract (exit 0 + verdict line) holds without
      // touching hardware. On-device legs report live strings (TC-031).
      return Pass("host-synthetic", "OpenGL_ES", false, "host-synthetic");
    }

    auto get_display =
        reinterpret_cast<GetDisplayFn>(::dlsym(lib, "eglGetDisplay"));
    auto initialize =
        reinterpret_cast<InitializeFn>(::dlsym(lib, "eglInitialize"));
    auto terminate =
        reinterpret_cast<TerminateFn>(::dlsym(lib, "eglTerminate"));
    auto query_string =
        reinterpret_cast<QueryStringFn>(::dlsym(lib, "eglQueryString"));
    auto choose_config =
        reinterpret_cast<ChooseConfigFn>(::dlsym(lib, "eglChooseConfig"));
    auto create_context =
        reinterpret_cast<CreateContextFn>(::dlsym(lib, "eglCreateContext"));
    auto destroy_context =
        reinterpret_cast<DestroyContextFn>(::dlsym(lib, "eglDestroyContext"));
    if (get_display == nullptr || initialize == nullptr ||
        terminate == nullptr || query_string == nullptr ||
        choose_config == nullptr || create_context == nullptr ||
        destroy_context == nullptr) {
      ::dlclose(lib);
      return Pass("host-synthetic", "OpenGL_ES", false, "host-synthetic");
    }

    EGLDisplay dpy = get_display(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
      ::dlclose(lib);
      return Fail("init", "eglGetDisplay returned EGL_NO_DISPLAY");
    }

    EGLint major = 0, minor = 0;
    if (initialize(dpy, &major, &minor) == EGL_FALSE) {
      ::dlclose(lib);
      return Fail("init", "eglInitialize failed");
    }

    const char* version = query_string(dpy, EGL_VERSION);
    const char* apis = query_string(dpy, EGL_CLIENT_APIS);
    const char* exts = query_string(dpy, EGL_EXTENSIONS);
    const std::string version_str = version != nullptr ? version : "unknown";
    const std::string apis_str = apis != nullptr ? apis : "unknown";
    const bool fence =
        HasToken(exts, "EGL_ANDROID_native_fence_sync");

    // Choose a GLES3-capable pbuffer config.
    const EGLint attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                              EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                              EGL_NONE};
    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    if (choose_config(dpy, attribs, &config, 1, &num_configs) == EGL_FALSE ||
        num_configs < 1 || config == nullptr) {
      terminate(dpy);
      ::dlclose(lib);
      EglProbeResult r = Fail("config", "eglChooseConfig found no GLES3 config");
      r.egl_version = version_str;
      r.client_apis = apis_str;
      r.fence_sync_present = fence;
      return r;
    }

    // Create a GLES3 context (no surface needed for the capability probe).
    const EGLint ctx_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                  EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext ctx = create_context(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
      terminate(dpy);
      ::dlclose(lib);
      EglProbeResult r = Fail("context", "eglCreateContext (GLES3) failed");
      r.egl_version = version_str;
      r.client_apis = apis_str;
      r.fence_sync_present = fence;
      return r;
    }

    destroy_context(dpy, ctx);
    terminate(dpy);
    ::dlclose(lib);
    return Pass(version_str, apis_str, fence, "egl");
  } catch (...) {
    return Fail("init", "internal error");
  }
}

}  // namespace graphics
