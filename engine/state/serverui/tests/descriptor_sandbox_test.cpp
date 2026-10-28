// Batch 4 (Server-Driven UI, FR-STATE-009) contract test — T10.
// Spec refs: FR-STATE-009, AC-STATE-038 (TC-STATE-038).
// Hermetic: C++17 + committed engine/state headers only; descriptors built
// in-test, no env/config/network.
// Binds ONLY to the T10 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T10.md):
//   A-X1: namespace muse::state; `Sandbox` + opcode model live in
//         `muse/state/sandbox.hpp`, logic in `serverui/src/sandbox.cpp`.
//         T1.10: canonical `Descriptor` is owned SOLELY by
//         `muse/state/descriptor.hpp` (envelope + addNode/nodeCount +
//         addOp/opCount); this test reaches it ONLY transitively via
//         `sandbox.hpp` (single canonical include path, never a redefinition).
//   A-X2: Descriptor exposes `void addOp(const char* opcode)` and
//         `std::size_t opCount() const` (opcode audit surface); descriptors
//         without native opcodes pass.
//   A-X3: Violation opcodes are exactly "loadLibrary" and "execCode"
//         (case-sensitive); any other opcode (including "render", "text",
//         "image") is benign.
//   A-X4: Sandbox::check is a static pure function of the descriptor (no
//         registry/signature input); rejection is Status::kCorrupt, never
//         throws; empty descriptor is kOk.
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/sandbox.hpp"  // pulls canonical descriptor.hpp transitively (T1.10)

namespace {

using muse::state::Descriptor;
using muse::state::Sandbox;
using muse::state::Status;

static Descriptor clean() {
  Descriptor d;
  d.addOp("render");
  d.addOp("text");
  d.addOp("image");
  return d;
}

// Pure UI opcodes pass the sandbox.
static bool check_clean_descriptor_passes() {
  return clean().opCount() == 3 && Sandbox::check(clean()) == Status::kOk;
}

// loadLibrary opcode is rejected even in isolation.
static bool check_load_library_rejected() {
  Descriptor d = clean();
  d.addOp("loadLibrary");
  return Sandbox::check(d) == Status::kCorrupt;
}

// execCode opcode is rejected even in isolation.
static bool check_exec_code_rejected() {
  Descriptor d = clean();
  d.addOp("execCode");
  return Sandbox::check(d) == Status::kCorrupt;
}

// Both violations together are still kCorrupt (single denial, no crash).
static bool check_both_violations_rejected() {
  Descriptor d;
  d.addOp("loadLibrary");
  d.addOp("execCode");
  return Sandbox::check(d) == Status::kCorrupt;
}

// Empty descriptor has zero native opcodes → kOk.
static bool check_empty_descriptor_passes() {
  Descriptor d;
  return Sandbox::check(d) == Status::kOk;
}

// Violation is rejected regardless of signature: construct the descriptor
// directly (as if signed) and the sandbox still denies it.
static bool check_signed_but_violating_still_rejected() {
  Descriptor d = clean();
  d.addOp("execCode");
  // NOTE: no signature input to Sandbox::check by design — W^X holds
  // regardless of trust. A signed envelope around these bytes must still
  // be denied here; verifyDescriptor's kOk must never override this.
  return Sandbox::check(d) == Status::kCorrupt;
}

// Near-miss opcode names ("loadlibrary" lowercase, "execcode") are benign:
// the allowlist is exact, case-sensitive, no substring matching.
static bool check_case_variant_opcodes_are_benign() {
  Descriptor d;
  d.addOp("loadlibrary");
  d.addOp("execcode");
  d.addOp("load_library");
  return Sandbox::check(d) == Status::kOk;
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_clean_descriptor_passes);
  RUN_CASE(check_load_library_rejected);
  RUN_CASE(check_exec_code_rejected);
  RUN_CASE(check_both_violations_rejected);
  RUN_CASE(check_empty_descriptor_passes);
  RUN_CASE(check_signed_but_violating_still_rejected);
  RUN_CASE(check_case_variant_opcodes_are_benign);
  if (failures > 0) {
    std::printf("descriptor_sandbox_test: %d failing\n", failures);
    return 1;
  }
  std::printf("descriptor_sandbox_test: all passing\n");
  return 0;
}
