// probe.c — T1 minimal 6562 EGL availability probe (FR-GRAPHICS-011, AC-031).
// Minimal EGL init only: display -> init -> log version/client APIs/
// EGL_ANDROID_native_fence_sync -> exit. No gallery code touched.
//
// Interface: `int main(int argc, char** argv)` — exit 0 + stdout
// `EGL-PROBE-VERDICT: PASS egl=<ver> client=<apis> fence=<yes|no>` on success;
// nonzero + stdout `EGL-PROBE-DIAGNOSTIC: <trigger>` on any failure
// (never crash/hang/silent skip).
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <EGL/egl.h>

#define PROBE_TIMEOUT_SECS 20

static int has_token(const char *list, const char *token) {
  size_t n;
  const char *p;
  if (!list || !token || !*token) return 0;
  n = strlen(token);
  p = list;
  while ((p = strstr(p, token)) != NULL) {
    if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return 1;
    p += n;
  }
  return 0;
}

int main(int argc, char **argv) {
  EGLDisplay dpy;
  EGLint major = 0, minor = 0;
  const char *client = NULL;
  const char *exts = NULL;
  const char *ver = NULL;
  int fence = 0;
  (void)argc;
  (void)argv;

  /* Time-box: never hang even if the driver blocks in egl calls. */
  alarm(PROBE_TIMEOUT_SECS);

  dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy == EGL_NO_DISPLAY) {
    printf("EGL-PROBE-DIAGNOSTIC: eglGetDisplay failed\n");
    fflush(stdout);
    return 1;
  }
  if (!eglInitialize(dpy, &major, &minor)) {
    printf("EGL-PROBE-DIAGNOSTIC: eglInitialize failed\n");
    fflush(stdout);
    return 1;
  }
  ver = eglQueryString(dpy, EGL_VERSION);
  client = eglQueryString(dpy, EGL_CLIENT_APIS);
  exts = eglQueryString(dpy, EGL_EXTENSIONS);
  if (!ver || !client) {
    printf("EGL-PROBE-DIAGNOSTIC: eglQueryString missing version/client-apis\n");
    fflush(stdout);
    eglTerminate(dpy);
    return 1;
  }
  fence = has_token(exts, "EGL_ANDROID_native_fence_sync");
  printf("EGL-PROBE-VERDICT: PASS egl=%d.%d client=%s fence=%s\n", major, minor,
         client, fence ? "yes" : "no");
  fflush(stdout);
  eglTerminate(dpy);
  return 0;
}
