// T4 contract tests — FR-M3-006, AC-M3-012, TC-M3-012 (device leg).
//
// Device-leg: caret/selection by touch, keyboard, API + binding report on
// the single probe page (`m3-text-probe`), API-29-only gate on
// 127.0.0.1:6569. Step sequence (device-observed, EXTERNAL — see contract
// T4): tap -> keyboard -> API set; mutate value; assert caret/selection
// survive + binding round-trip report; repaint <= 1 frame, no
// re-instantiation. Exit contract: same exit/failure contract as T3 — `0`
// pass, nonzero per-step fail; any failure appends `evidence/T4/
// failure.log` with step + dumpsys + screencap PNG path.
//
// Hermetic host scope of THIS file: the fail-fast / exit-code / no-throw
// contract of the entry. These tests NEVER pass the gate serial, so no adb
// traffic, no device, no network, no env/config reads, no filesystem.
// The gate-serial path (real adb leg) is EXTERNAL, owned by the tester on
// device 127.0.0.1:6569.
//
// Binds to the T4 `Interface:` line (verbatim in contract T4):
//   `int RunSelectionLeg(const char* serial)` — same exit/failure contract
//   as T3.
// The production TU does not define it yet, so the test forward-declares
// the entry (same pattern as textfield_probe_attach_test.cpp). The gate
// serial literal below cites plan Settled BQ2; it is the test's proposal
// for the exact string constant (see ASSUMPTION in contract T4).
//
// ASSUMPTION (gate literal): the API-29-only gate serial is the string
// "127.0.0.1:6569". The contract is "exactly that serial may attempt the
// device leg; every other serial fails fast with nonzero, no throw, and no
// device contact".

#include <cstdio>

// Production entry (coder-owned). Forward-declared so this test binds to
// the published signature, not to an implementation detail.
int RunSelectionLeg(const char* serial);

namespace {

constexpr const char* kGateSerial = "127.0.0.1:6569";

// Fail-closed: null serial -> nonzero, no throw, no device contact.
bool check_selection_null_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunSelectionLeg(nullptr);
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Fail-closed: empty serial -> nonzero, no throw.
bool check_selection_empty_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunSelectionLeg("");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Gate scope (BQ2): same host, wrong port -> nonzero, no throw.
bool check_selection_wrong_port_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunSelectionLeg("127.0.0.1:6570");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// Gate scope (BQ2): unknown serial -> nonzero, no throw.
bool check_selection_unknown_serial_fails() {
  bool threw = false;
  int rc = 0;
  try {
    rc = RunSelectionLeg("emulator-5554");
  } catch (...) {
    threw = true;
  }
  return !threw && rc != 0;
}

// JNI/host boundary safety: never throws across all bad-serial inputs.
bool check_selection_never_throws() {
  static const char* const kBad[] = {nullptr, "", "127.0.0.1:6570",
                                     "emulator-5554", "not-a-serial"};
  for (const char* s : kBad) {
    try {
      (void)RunSelectionLeg(s);
    } catch (...) {
      return false;
    }
  }
  return true;
}

// Determinism: the same invalid serial fails the same way twice (stable
// per-step nonzero contract, no order dependence, no shared state).
bool check_selection_fail_code_deterministic() {
  int first = 0;
  int second = 0;
  try {
    first = RunSelectionLeg("127.0.0.1:6570");
    second = RunSelectionLeg("127.0.0.1:6570");
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
  RUN_CASE(check_selection_null_serial_fails);
  RUN_CASE(check_selection_empty_serial_fails);
  RUN_CASE(check_selection_wrong_port_serial_fails);
  RUN_CASE(check_selection_unknown_serial_fails);
  RUN_CASE(check_selection_never_throws);
  RUN_CASE(check_selection_fail_code_deterministic);
  if (failures > 0) {
    std::printf("textfield_probe_selection_test: %d failing\n", failures);
    return 1;
  }
  std::printf("textfield_probe_selection_test: all passing\n");
  return 0;
}
