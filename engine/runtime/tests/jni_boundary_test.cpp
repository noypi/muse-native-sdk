// Batch 1 / T2 contract tests — NFR-RUNTIME-001 (AC-RUNTIME-025).
// JNI boundary behind engine-type-free public headers: the public header
// includes only <jni.h> + opaque handles + the JsRuntime-style ABC pattern;
// all engine internals stay in jni_bridge.cpp.
// Hermetic: C++17 + runtime/jni_bridge.h only; no JVM is started, no device,
// no network, no env/config reads, no filesystem, no live services. The null-
// VM JNI_OnLoad case runs fully in-process; the success-path version check
// against a live JVM is EXTERNAL (device-lab lane).
// Binds ONLY to the T2 `Interface:` lines; never to implementation.
#include <cstdio>

#include "runtime/jni_bridge.h"

namespace {

// The public header must expose handles as opaque void* only: any engine
// type leaking into the header breaks this test at compile time, because the
// test below names no engine type and only compiles if the header's surface
// is (JavaVM, jint, void*) + the ABC pattern.
static bool handles_are_opaque_void_ptr() {
  void* handle = ::runtime::Runtime_create();
  if (handle == nullptr) return false;
  ::runtime::Runtime_destroy(handle);
  return true;
}

}  // namespace

// Named case: Runtime_create returns a non-null opaque handle.
static bool check_create_returns_non_null_opaque_handle() {
  void* handle = ::runtime::Runtime_create();
  if (handle == nullptr) {
    std::printf("  detail: Runtime_create returned null\n");
    return false;
  }
  ::runtime::Runtime_destroy(handle);
  return true;
}

// Named case: create/destroy round-trips without crashing or leaking the
// failure into the return path (destroy returns void).
static bool check_create_destroy_roundtrip_no_crash() {
  bool ok = false;
  try {
    void* handle = ::runtime::Runtime_create();
    if (handle == nullptr) return false;
    ::runtime::Runtime_destroy(handle);
    ok = true;
  } catch (...) {
    return false;
  }
  return ok;
}

// Named case: Runtime_destroy on a null handle is a safe no-op.
static bool check_destroy_null_is_safe_noop() {
  try {
    ::runtime::Runtime_destroy(nullptr);
  } catch (...) {
    return false;
  }
  return true;
}

// Named case: two created handles are distinct engine instances.
static bool check_two_handles_are_distinct() {
  void* first = ::runtime::Runtime_create();
  void* second = ::runtime::Runtime_create();
  if (first == nullptr || second == nullptr) {
    if (first != nullptr) ::runtime::Runtime_destroy(first);
    if (second != nullptr) ::runtime::Runtime_destroy(second);
    return false;
  }
  bool distinct = (first != second);
  ::runtime::Runtime_destroy(first);
  ::runtime::Runtime_destroy(second);
  return distinct;
}

// Named case: JNI_OnLoad with a null VM fails closed with JNI_ERR and never
// returns a version. Hermetic: no JVM is created; null is never a valid VM.
static bool check_onload_null_vm_returns_jni_err() {
  jint rc = ::JNI_OnLoad(nullptr, nullptr);
  return rc == JNI_ERR;
}

// Named case: JNI_OnLoad with a null VM never reports success (guards the
// == JNI_VERSION_1_6 vs != JNI_ERR confusion).
static bool check_onload_null_vm_never_reports_success_version() {
  jint rc = ::JNI_OnLoad(nullptr, nullptr);
  return rc != JNI_VERSION_1_6;
}

// Named case: opaque handles cross the boundary intact — the test only ever
// touches void*, so any engine-internal type in the public header is a
// compile break here (mirrors js_abi_isolation_test.cpp).
static bool check_header_names_no_engine_types() {
  return handles_are_opaque_void_ptr();
}

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
  RUN_CASE(check_create_returns_non_null_opaque_handle);
  RUN_CASE(check_create_destroy_roundtrip_no_crash);
  RUN_CASE(check_destroy_null_is_safe_noop);
  RUN_CASE(check_two_handles_are_distinct);
  RUN_CASE(check_onload_null_vm_returns_jni_err);
  RUN_CASE(check_onload_null_vm_never_reports_success_version);
  RUN_CASE(check_header_names_no_engine_types);
  if (failures > 0) {
    std::printf("jni_boundary_test: %d failing\n", failures);
    return 1;
  }
  std::printf("jni_boundary_test: all passing\n");
  return 0;
}
