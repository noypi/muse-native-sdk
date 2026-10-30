#ifndef GRAPHICS_EGL_PROBE_EGL_PROBE_H_
#define GRAPHICS_EGL_PROBE_EGL_PROBE_H_

#include <string>

namespace graphics {

// Options controlling the EGL capability probe.
struct EglProbeOptions {
  bool force_fail_init = false;
  bool report_drivers = false;
  std::string device_serial;
};

// Result of the EGL capability probe. `exit_code` is 0 on pass with
// `verdict_line` starting `EGL-PROBE-VERDICT:`, nonzero on any failure
// with `diagnostic` naming the stage (`init|config|context|surface`).
struct EglProbeResult {
  int exit_code = 1;
  std::string verdict_line;
  std::string egl_version;
  std::string client_apis;
  bool fence_sync_present = false;
  std::string diagnostic;
};

// Runs the EGL probe. Never throws, never blocks on input, never
// silently skips: every return carries an explicit verdict line.
EglProbeResult run_egl_probe(const EglProbeOptions& opts) noexcept;

}  // namespace graphics

#endif  // GRAPHICS_EGL_PROBE_EGL_PROBE_H_
