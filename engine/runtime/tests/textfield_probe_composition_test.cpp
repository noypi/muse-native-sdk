// T3 contract tests — FR-M3-006, AC-M3-011, TC-M3-011 (device leg).
//
// Device-leg: IME composition preview/commit/cancel on the single probe page
// (`m3-text-probe`), API-29-only gate on 127.0.0.1:6569. Step sequence
// (device-observed, EXTERNAL — see contract T3):
//   setComposingText -> preview (composing underline in PNG) -> commit ->
//   bound==visible at every step -> setComposing + cancel -> withdraw.
// Exit contract: `0` pass, nonzero per-step fail; any failure appends
// `evidence/T3/failure.log` with step + dumpsys + screencap PNG path.
//
// Hermetic host scope of THIS file: the fail-fast / exit-code / no-throw
// contract of the entry. These tests NEVER pass the gate serial, so no adb
// traffic, no device, no network, no env/config reads, no filesystem.
// The gate-serial path (real adb leg) is EXTERNAL, owned by the tester on
// device 127.0.0.1:6569.
//
// Binds to the T3 `Interface:` line (verbatim in contract T3):
//   `int RunCompositionLeg(const char* serial)` — `0` pass, nonzero
//   per-step fail + `evidence/T3/failure.log`.
// The production TU does not define it yet, so the test forward-declares
// the entry (same pattern as textfield_probe_attach_test.cpp). The gate
// serial literal below cites plan Settled BQ2; it is the test's proposal
// for the exact string constant (see ASSUMPTION in contract T3).
//
// ASSUMPTION (gate literal): the API-29-only gate serial is the string
// "127.0.0.1:6569". The contract is "exactly that serial may attempt the
// device leg; every other serial fails fast with nonzero, no throw, and no
// device contact".

#include <cstdio>

// Production entry (coder-owned). Forward-declared so this test binds to
// the published signature, not to an implementation detail.
int RunCompositionLeg(const char* serial);

namespace {

constexpr const char* kGateSerial = "127.0.0.1:6569";

// Fail-closed: null serial -> nonzero, no throw, no device contact.
bool check_composition_null_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunCompositionLeg(nullptr);
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Fail-closed: empty serial -> nonzero, no throw.
bool check_composition_empty_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunCompositionLeg("");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Gate scope (BQ2): same host, wrong port -> nonzero, no throw.
bool check_composition_wrong_port_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunCompositionLeg("127.0.0.1:6570");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Gate scope (BQ2): unknown serial -> nonzero, no throw.
bool check_composition_unknown_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunCompositionLeg("emulator-5554");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// JNI/host boundary safety: never throws across all bad-serial inputs.
bool check_composition_never_throws() {
  static const char* const kBad[] = {nullptr, "", "127.0.0.1:6570",
                                     "emulator-5554", "not-a-serial"};
  for (const char* s : kBad) {
    try {
      (void)RunCompositionLeg(s);
    } catch (...) {
      return false;
    }
  }
  return true;
}

// Determinism: the same invalid serial fails the same way twice (stable
// per-step nonzero contract, no order dependence, no shared state).
bool check_composition_fail_code_deterministic() {
  int first = 0;
  int second = 0;
  try {
    first = RunCompositionLeg("127.0.0.1:6570");
    second = RunCompositionLeg("127.0.0.1:6570");
  } catch (...) {
    return false;
  }
  return first != 0 && first == second;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  (void)kGateSerial;  // gate literal pinned for the device leg; host checks
                      // below never dial it, so no adb traffic occurs here.
  int failures = 0;
  RUN_CASE(check_composition_null_serial_fails);
  RUN_CASE(check_composition_empty_serial_fails);
  RUN_CASE(check_composition_wrong_port_serial_fails);
  RUN_CASE(check_composition_unknown_serial_fails);
  RUN_CASE(check_composition_never_throws);
  RUN_CASE(check_composition_fail_code_deterministic);
  if (failures > 0) {
    std::printf("textfield_probe_composition_test: %d failing\n", failures);
    return 1;
  }
  std::printf("textfield_probe_composition_test: all passing\n");
  return 0;
}
