// T8 contract tests — FR-TEXT-013 / AC-TEXT-033 (TC-TEXT-033).
// Binds ONLY to the T8 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/autofill.h` does not exist yet, so this
// file must FAIL TO COMPILE. That is the recorded red state.
//
// ASSUMPTION (namespace): `AutofillHint`/`AutofillField`/`set_autofill_hint`/
// `apply_autofill_dataset` are referenced unqualified (global namespace,
// matching the GapBuffer/ALooperBridge precedent). If the coder nests them
// (e.g. `runtime::`), call sites reconcile at verify — expectations do not
// change.
// ASSUMPTION (insert position): the Interface line does not pin where the
// dataset value lands in the buffer. The tests assert the single-field case
// on an empty buffer (only one place to land) and multi-field concatenation
// in application order (append-at-end). If the coder binds insertion to a
// cursor/selection instead, the multi-field expectation reconciles at
// verify — the single-field and unknown-hint expectations do not change.
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/autofill.h"
#include "runtime/gap_buffer.h"

// --- harness cases (match existing test style) -----------------------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T8 cases (each maps to the Interface: line + AC-TEXT-033) --------------

// Happy path: setting a hint stores it on the field.
static bool check_set_hint_stores_hint() {
  AutofillField f;
  f.field_id = 1;
  set_autofill_hint(f, AutofillHint::kUsername);
  if (f.hint != AutofillHint::kUsername) return false;
  set_autofill_hint(f, AutofillHint::kPassword);
  return f.hint == AutofillHint::kPassword;
}

// Happy path: every mapped hint kind is accepted by set_autofill_hint.
static bool check_set_hint_accepts_all_mapped_kinds() {
  AutofillField f;
  set_autofill_hint(f, AutofillHint::kEmail);
  if (f.hint != AutofillHint::kEmail) return false;
  set_autofill_hint(f, AutofillHint::kPhone);
  if (f.hint != AutofillHint::kPhone) return false;
  set_autofill_hint(f, AutofillHint::kNone);
  return f.hint == AutofillHint::kNone;
}

// Happy path: a dataset commit for a mapped hint lands the value and
// returns true.
static bool check_apply_dataset_lands_value() {
  GapBuffer buf;
  AutofillField f;
  f.field_id = 7;
  f.value = "user@example.test";
  set_autofill_hint(f, AutofillHint::kEmail);
  if (!apply_autofill_dataset(buf, f)) return false;
  return buf.to_string() == "user@example.test";
}

// Happy path: password dataset lands verbatim (no masking in the buffer).
static bool check_apply_password_dataset_lands_verbatim() {
  GapBuffer buf;
  AutofillField f;
  f.field_id = 2;
  f.value = "s3cr3t-not-real";
  set_autofill_hint(f, AutofillHint::kPassword);
  if (!apply_autofill_dataset(buf, f)) return false;
  return buf.to_string() == "s3cr3t-not-real";
}

// Acceptance: one-tap multi-field fill lands every value in the right
// buffer, in application order.
static bool check_multi_field_fill_lands_every_value() {
  GapBuffer user_buf;
  GapBuffer pass_buf;
  AutofillField u;
  u.field_id = 1;
  u.value = "alice.test";
  set_autofill_hint(u, AutofillHint::kUsername);
  AutofillField p;
  p.field_id = 2;
  p.value = "pw-not-real";
  set_autofill_hint(p, AutofillHint::kPassword);
  if (!apply_autofill_dataset(user_buf, u)) return false;
  if (!apply_autofill_dataset(pass_buf, p)) return false;
  return user_buf.to_string() == "alice.test" &&
         pass_buf.to_string() == "pw-not-real";
}

// Acceptance: sequential datasets on one buffer accumulate in order.
static bool check_sequential_datasets_accumulate_in_order() {
  GapBuffer buf;
  AutofillField a;
  a.field_id = 1;
  a.value = "one-";
  set_autofill_hint(a, AutofillHint::kUsername);
  AutofillField b;
  b.field_id = 2;
  b.value = "two";
  set_autofill_hint(b, AutofillHint::kEmail);
  if (!apply_autofill_dataset(buf, a)) return false;
  if (!apply_autofill_dataset(buf, b)) return false;
  return buf.to_string() == "one-two";
}

// Boundary: unknown/unmapped hint (kNone) returns false, buffer untouched.
static bool check_unmapped_hint_returns_false_untouched() {
  GapBuffer buf;
  buf.insert(0, "keep");
  AutofillField f;
  f.field_id = 9;
  f.value = "should-not-land";
  set_autofill_hint(f, AutofillHint::kNone);
  if (apply_autofill_dataset(buf, f)) return false;
  return buf.to_string() == "keep";
}

// Boundary: empty dataset value on a mapped hint is still accepted.
static bool check_empty_value_on_mapped_hint() {
  GapBuffer buf;
  buf.insert(0, "keep");
  AutofillField f;
  f.field_id = 3;
  f.value = "";
  set_autofill_hint(f, AutofillHint::kPhone);
  const std::string before = buf.to_string();
  bool ok = false;
  try {
    ok = apply_autofill_dataset(buf, f);
  } catch (...) {
    return false;
  }
  return ok && buf.to_string() == before;
}

// Error path: extreme inputs never throw and leave a usable buffer.
static bool check_extreme_inputs_never_throw() {
  GapBuffer buf;
  try {
    AutofillField f;
    f.field_id = -2147483647;
    f.value = std::string(65536, 'x');
    set_autofill_hint(f, AutofillHint::kUsername);
    apply_autofill_dataset(buf, f);
    AutofillField g;
    g.field_id = 2147483647;
    g.value = "";
    set_autofill_hint(g, AutofillHint::kNone);
    if (apply_autofill_dataset(buf, g)) return false;
    (void)buf.to_string();
  } catch (...) {
    return false;
  }
  return true;
}

// S5 (security LOW — payload caps): a single autofill value above 4MB is
// rejected — `apply_autofill_dataset` returns false, buffer unchanged, no
// throw. The existing 64KB extreme-input case above is unaffected.
static bool check_oversize_autofill_value_rejected() {
  GapBuffer buf;
  buf.insert(0, "keep");
  AutofillField f;
  f.field_id = 1;
  set_autofill_hint(f, AutofillHint::kUsername);
  f.value = std::string(8 * 1024 * 1024, 'x');
  bool ok = true;
  try {
    ok = apply_autofill_dataset(buf, f);
  } catch (...) {
    return false;
  }
  return !ok && buf.to_string() == "keep";
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_set_hint_stores_hint);
  RUN_CASE(check_set_hint_accepts_all_mapped_kinds);
  RUN_CASE(check_apply_dataset_lands_value);
  RUN_CASE(check_apply_password_dataset_lands_verbatim);
  RUN_CASE(check_multi_field_fill_lands_every_value);
  RUN_CASE(check_sequential_datasets_accumulate_in_order);
  RUN_CASE(check_unmapped_hint_returns_false_untouched);
  RUN_CASE(check_empty_value_on_mapped_hint);
  RUN_CASE(check_extreme_inputs_never_throw);
  RUN_CASE(check_oversize_autofill_value_rejected);
  if (failures > 0) {
    std::printf("autofill_test: %d failing\n", failures);
    return 1;
  }
  std::printf("autofill_test: all passing\n");
  return 0;
}
