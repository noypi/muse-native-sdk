// Batch 1 contract tests — T1 egl-probe force-fail (FR-GRAPHICS-011).
// Binds ONLY to the T1 `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + planned egl-probe headers only; no network, no env/
// config reads, no filesystem, no live services, no subprocesses.
// Expected state before implementation: RED — `egl-probe/egl_probe.h`
// does not exist, so compilation fails (missing behavior, not a malformed
// test). The coder provides the headers/sources; these tests then compile
// and run unmodified (call-site reconciliation only, per ASSUMPTIONs).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "egl-probe/egl_probe.h"

// --- harness cases (repo convention, cf. engine/graphics/device) ------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T1 helpers ------------------------------------------------------------
// ASSUMPTION (A-T1-SHAPE): C++ spelling of the plan interface. Plan line:
// `EglProbeResult run_egl_probe(const EglProbeOptions& opts)` where
// `EglProbeOptions{bool force_fail_init; bool report_drivers;
// std::string device_serial}` and `EglProbeResult{int exit_code;
// std::string verdict_line; std::string egl_version; std::string client_apis;
// bool fence_sync_present; std::string diagnostic}`. The plan/spec do not
// settle namespace or aggregate construction; tests assume
// `graphics::EglProbeOptions` / `graphics::EglProbeResult` aggregates with
// exactly those field names and `graphics::run_egl_probe(const
// EglProbeOptions&)` at namespace scope. If the coder settles a different
// spelling, update call sites only — expectations are unchanged.

static graphics::EglProbeOptions make_opts(bool force_fail_init) {
  graphics::EglProbeOptions o;
  o.force_fail_init = force_fail_init;
  o.report_drivers = true;
  o.device_serial = "host-test";
  return o;
}

static bool starts_with(const std::string& s, const char* prefix) {
  return s.compare(0, std::strlen(prefix), prefix) == 0;
}

// --- T1 behavior cases (AC-GRAPHICS-031/032, TC-GRAPHICS-032) ---------------

// AC-032: forced init failure exits nonzero with a diagnostic naming `init`.
static bool check_force_fail_init_exits_nonzero_with_init_diagnostic() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  if (r.exit_code == 0) return false;
  if (r.diagnostic.find("init") == std::string::npos) return false;
  if (!starts_with(r.verdict_line, "EGL-PROBE-VERDICT:")) return false;
  return true;
}

// AC-031: clean pass exits 0 with a verdict line (hermetic host path;
// on-device EGL strings are covered by TC-031, marked EXTERNAL).
static bool check_clean_pass_exits_zero_with_verdict_line() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(false /* force_fail_init */));
  if (r.exit_code != 0) return false;
  if (!starts_with(r.verdict_line, "EGL-PROBE-VERDICT:")) return false;
  return true;
}

// AC-032: failure diagnostic names the stage (`init|config|context|surface`).
static bool check_failure_diagnostic_names_stage() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  if (r.exit_code == 0) return false;
  const char* stages[] = {"init", "config", "context", "surface"};
  for (const char* s : stages) {
    if (r.diagnostic.find(s) != std::string::npos) return true;
  }
  return false;
}

// AC-032 contract: never throws, even on the forced-failure path.
static bool check_probe_never_throws() {
  try {
    volatile graphics::EglProbeResult r1 =
        graphics::run_egl_probe(make_opts(false));
    (void)r1;
    volatile graphics::EglProbeResult r2 =
        graphics::run_egl_probe(make_opts(true));
    (void)r2;
  } catch (...) {
    return false;
  }
  return true;
}

// AC-032 contract: never blocks on input — the call returns (the timeout
// guard in run-efail.sh is the mechanical backstop; this asserts return).
static bool check_probe_returns_without_input() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  (void)r;
  return true;  // reaching here proves return without stdin.
}

// AC-032 contract: never silently skips — every result carries an explicit
// verdict line plus a nonzero exit on failure.
static bool check_probe_never_silently_skips() {
  graphics::EglProbeResult fail =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  if (fail.exit_code == 0) return false;
  if (!starts_with(fail.verdict_line, "EGL-PROBE-VERDICT:")) return false;
  graphics::EglProbeResult pass =
      graphics::run_egl_probe(make_opts(false));
  if (!starts_with(pass.verdict_line, "EGL-PROBE-VERDICT:")) return false;
  return true;
}

// Security (GR-05 R5.3): diagnostic names the stage only — no secret/config
// dump. The forced-init path must not echo env-derived secrets.
static bool check_diagnostic_leaks_no_secrets() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  if (r.diagnostic.find("init") == std::string::npos) return false;
  if (r.diagnostic.find("KEY") != std::string::npos) return false;
  if (r.diagnostic.find("TOKEN") != std::string::npos) return false;
  if (r.diagnostic.find("SECRET") != std::string::npos) return false;
  return true;
}

// Security (5): init diagnostic is exactly stage + fixed reason — no
// internals, no config/context material. Pins the Fail("init", ...) literal.
static bool check_init_diagnostic_exact_fixed_shape() {
  graphics::EglProbeResult r =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  const std::string want =
      "EGL-PROBE-DIAGNOSTIC: init forced failure before eglGetDisplay";
  if (r.diagnostic != want) return false;
  if (r.verdict_line != "EGL-PROBE-VERDICT: FAIL init") return false;
  return true;
}

// Security (5): even with a planted env secret, no failure or clean path
// echoes it into diagnostic/verdict. run_egl_probe never reads env.
static bool check_diagnostic_never_echoes_env_secret() {
  const char* planted = "s3cr3t-planted-xyz-123";
#if defined(_WIN32)
  (void)planted;
  return true;  // setenv-based planting is POSIX-only; shape pinned above.
#else
  ::setenv("EFAIL_PLANTED_SECRET_FOR_TEST", planted, 1);
  graphics::EglProbeResult fail =
      graphics::run_egl_probe(make_opts(true /* force_fail_init */));
  graphics::EglProbeResult pass =
      graphics::run_egl_probe(make_opts(false));
  ::unsetenv("EFAIL_PLANTED_SECRET_FOR_TEST");
  if (fail.diagnostic.find(planted) != std::string::npos) return false;
  if (fail.verdict_line.find(planted) != std::string::npos) return false;
  if (pass.diagnostic.find(planted) != std::string::npos) return false;
  if (pass.verdict_line.find(planted) != std::string::npos) return false;
  return true;
#endif
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_force_fail_init_exits_nonzero_with_init_diagnostic);
  RUN_CASE(check_clean_pass_exits_zero_with_verdict_line);
  RUN_CASE(check_failure_diagnostic_names_stage);
  RUN_CASE(check_probe_never_throws);
  RUN_CASE(check_probe_returns_without_input);
  RUN_CASE(check_probe_never_silently_skips);
  RUN_CASE(check_diagnostic_leaks_no_secrets);
  RUN_CASE(check_init_diagnostic_exact_fixed_shape);
  RUN_CASE(check_diagnostic_never_echoes_env_secret);
  if (failures > 0) {
    std::printf("egl_probe_force_fail.test: %d failing\n", failures);
    return 1;
  }
  std::printf("egl_probe_force_fail.test: all passing\n");
  return 0;
}
