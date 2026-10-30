// Batch 2 / T3 contract tests — FR-INPUT-011 / AC-INPUT-020 (device tap
// chain, JNI entry half).
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no JVM is started, no device, no network, no env/
// config reads, no filesystem, no live services. Every case calls the real
// JNI symbol with a null JNIEnv, so no VM interaction is possible and no
// exception can cross into a VM. The valid-env halves (pending-exception
// guard, real dispatch to a node, MainActivity.java forwarding) cannot run
// without a VM and are ASSUMPTIONs mapped to T7 device proof.
#include <cstdint>
#include <cstdio>

// Minimal JNI-compatible declarations (no <jni.h> on the hermetic path).
// The production definition in jni_bridge.cpp is `extern "C"`, so this
// declaration binds to the same unmangled symbol without needing jni.h.
// Only nullptr is ever passed as env, so the incomplete types are never
// dereferenced here.
struct _JNIEnv;
struct _jobject;
using JNIEnv = _JNIEnv;
using jobject = _jobject*;
using jlong = long;
using jint = int;
using jfloat = float;

extern "C" jint Java_com_muse_host_MainActivity_nativeDispatchTouch(
    JNIEnv* env, jobject thiz, jlong handle, jint action, jfloat x, jfloat y,
    jint pointerId, jlong timestampMs);

namespace {

// MotionEvent action codes (platform constants, repeated here so the test
// stays hermetic without android/view headers).
constexpr jint kActionDown = 0;
constexpr jint kActionUp = 1;

// Named case: null env + zero handle fails closed with -1 (the T3
// `Interface:` line: "null env/pending exception/zero handle fail closed
// with no exception crossing into the VM"). No VM exists here, so returning
// -1 without crashing is the whole assertion.
bool check_null_env_zero_handle_returns_minus_one() {
  const jint got = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, 0, kActionDown, 10.0f, 10.0f, 0, 0);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: null env + nonzero handle still fails closed with -1. The env
// guard must precede any handle use; a nonzero handle must not smuggle a
// dispatch through a null VM.
bool check_null_env_nonzero_handle_returns_minus_one() {
  const jint got = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, 0x1234, kActionDown, 10.0f, 10.0f, 0, 0);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: null env + inside-button coordinates + ACTION_DOWN returns -1.
// Without a VM nothing may resolve or dispatch; coordinates must never be
// trusted before the env guard.
bool check_null_env_inside_tap_returns_minus_one() {
  const jint got = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, 0x1234, kActionDown, 150.0f, 140.0f, 1, 123456789);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: null env + ACTION_UP + far-outside coordinates returns -1.
// No phantom dispatch and no crash for the release-side action either.
bool check_null_env_up_outside_returns_minus_one() {
  const jint got = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, 0, kActionUp, 10000.0f, 10000.0f, 0, 0);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
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
  RUN_CASE(check_null_env_zero_handle_returns_minus_one);
  RUN_CASE(check_null_env_nonzero_handle_returns_minus_one);
  RUN_CASE(check_null_env_inside_tap_returns_minus_one);
  RUN_CASE(check_null_env_up_outside_returns_minus_one);
  if (failures > 0) {
    std::printf("jni_touch_dispatch_shape_test: %d failing\n", failures);
    return 1;
  }
  std::printf("jni_touch_dispatch_shape_test: all passing\n");
  return 0;
}
