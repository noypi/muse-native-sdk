// egl-probe host CLI (FR-GRAPHICS-011, AC-031/032).
// Prints EGL version / client APIs / fence-sync presence plus one
// `EGL-PROBE-VERDICT: PASS|FAIL <stage>` line; `--force-fail <stage>`
// exits nonzero with an `EGL-PROBE-DIAGNOSTIC: <stage> ...` line.

#include <cstring>
#include <iostream>
#include <string>

#include "egl-probe/egl_probe.h"

int main(int argc, char** argv) {
  std::string force_stage;
  bool report_drivers = false;
  std::string device_serial;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] != nullptr ? argv[i] : "";
    if ((arg == "--force-fail" || arg == "--force_fail") && i + 1 < argc) {
      force_stage = argv[++i] != nullptr ? argv[i] : "";
    } else if (arg == "--report" && i + 1 < argc) {
      const std::string what = argv[++i] != nullptr ? argv[i] : "";
      if (what == "drivers") report_drivers = true;
    } else if ((arg == "--device" || arg == "--serial") && i + 1 < argc) {
      device_serial = argv[++i] != nullptr ? argv[i] : "";
    }
  }

  graphics::EglProbeOptions opts;
  opts.report_drivers = report_drivers;
  opts.device_serial = device_serial;
  opts.force_fail_init = (force_stage == "init");

  graphics::EglProbeResult r = graphics::run_egl_probe(opts);

  // Per-stage CLI legs: only `init` short-circuits inside run_egl_probe;
  // the config|context|surface legs force the failure path here so every
  // forced stage exits nonzero with a stage-naming diagnostic.
  if (!force_stage.empty() && force_stage != "init") {
    if (r.exit_code == 0 ||
        r.diagnostic.find(force_stage) == std::string::npos) {
      r.exit_code = 2;
      r.verdict_line = "EGL-PROBE-VERDICT: FAIL " + force_stage;
      r.diagnostic =
          "EGL-PROBE-DIAGNOSTIC: " + force_stage + " forced failure via CLI";
    }
  }

  if (report_drivers || r.exit_code == 0) {
    std::cout << "EGL-VERSION: "
              << (r.egl_version.empty() ? "unknown" : r.egl_version) << "\n";
    std::cout << "EGL-CLIENT-APIS: "
              << (r.client_apis.empty() ? "unknown" : r.client_apis) << "\n";
    std::cout << "EGL-FENCE-SYNC: "
              << (r.fence_sync_present ? "present" : "absent") << "\n";
  }
  std::cout << r.verdict_line << "\n";
  if (!r.diagnostic.empty()) std::cout << r.diagnostic << "\n";
  return r.exit_code;
}
