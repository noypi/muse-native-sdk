// Gallery registry-lifetime entry-shape tests — BUG-M3-FIDELITY-007 T5
// security re-audit follow-up (MEDIUM: ToGallery waiter UAF — registry lock
// released before use; racing ClaimForDestroy erases+deletes while an
// earlier holder waits on g->mu; fix: counted lifetime).
//
// What this file CAN pin hermetically: the null-env fail-closed shape of the
// two JNI entries that guard the registry (`nativeDestroy`,
// `nativeDispatchTouch`). Every case calls the REAL entry points (linked from
// the static `runtime` lib, same as jni_touch_dispatch_shape_test.cpp) with a
// null JNIEnv, so no VM is possible and no exception can cross into a VM.
//
// What this file CANNOT pin (deliberately absent, see T5.md ASSUMPTION → T7):
// - `ToGallery` / `ClaimForDestroy` / `g_liveHandles` / `GalleryHandle`
//   live in an anonymous namespace in jni_bridge.cpp (file lines ~193-330;
//   `nm -C libruntime.a` shows them as LOCAL `t` symbols). No test TU can
//   name them; only the extern "C" JNI entries are visible. Even with real
//   <jni.h> (as opposed to the minimal decls below) the registry symbols
//   remain unlinkable by design.
// - A LIVE handle can only be minted by `nativeLoadTree` with a valid
//   JNIEnv* (GetArrayLength/GetByteArrayRegion). Forged jlongs never enter
//   the live set, so "first destroy tears down exactly once" and
//   "destroy-then-use fails closed on a once-live handle" need a JVM.
// - The waiter race needs two threads with valid attached JNIEnv* racing a
//   dispatch blocked on g->mu against a destroy (erase + delete) — timing
//   dependent and VM-gated. A hermetic std::thread model would test a copy
//   of the protocol, never the production registry, so it is not written.
// - Null-env entries short-circuit BEFORE touching the registry
//   (env == nullptr → return), so these cases hold identically before and
//   after the counted-lifetime fix: they pin the fail-closed shape the fix
//   must preserve, they do not prove the fix.
//
// Hermetic: C++17 only; no JVM, no device, no network, no env/config reads,
// no filesystem, no live services. C++17 + committed sources only.
#include <cstdint>
#include <cstdio>

// Minimal JNI-compatible declarations (no <jni.h> on the hermetic path —
// same precedent as jni_touch_dispatch_shape_test.cpp). Only nullptr is ever
// passed as env, so the incomplete types are never dereferenced here.
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

extern "C" void Java_com_muse_host_MainActivity_nativeDestroy(
    JNIEnv* env, jobject thiz, jlong handle);

namespace {

// MotionEvent action codes (platform constants, mirrored from
// jni_bridge.cpp; repeated here so the test stays hermetic without
// android/view headers).
constexpr jint kActionDown = 0;
constexpr jint kActionUp = 1;

// Forged (never-live) handle values. These never enter g_liveHandles, so
// every entry must fail closed on them without a dereference.
constexpr jlong kForgedA = 0x1234;
constexpr jlong kForgedB = 0x5678;

// Inside-button coordinates under the T4/T5 seeded layout shape
// (400x800 root, button at (50,100)-(250,180)); trusted only after the
// env/handle guards pass, so null-env must still yield -1 here.
constexpr jfloat kInsideX = 150.0f;
constexpr jfloat kInsideY = 140.0f;

// Sequential double-destroy of the same forged handle fails closed twice:
// no crash, no teardown, no exception into a VM. (Counted-lifetime shape
// the fix must preserve: a second destroy of one handle fails closed.)
bool check_double_destroy_same_forged_handle_no_crash() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedA);
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedA);
  return true;  // reaching here without crashing is the assertion
}

// Sequential double-destroy of the zero handle is a no-op (never live).
bool check_double_destroy_zero_handle_no_crash() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, 0);
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, 0);
  return true;
}

// Destroy-then-use fails closed: after destroy, both DOWN and UP dispatches
// on the same forged handle return -1 (no phantom dispatch armed).
bool check_destroy_then_dispatch_fails_closed() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedA);
  const jint down = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedA, kActionDown, kInsideX, kInsideY, 1, 100);
  const jint up = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedA, kActionUp, kInsideX, kInsideY, 1, 200);
  if (down != -1 || up != -1) {
    std::printf("  detail: expected -1/-1, got %d/%d\n", static_cast<int>(down),
                static_cast<int>(up));
    return false;
  }
  return true;
}

// Dispatch → destroy → dispatch: every leg fails closed with -1 / no-op.
bool check_dispatch_destroy_dispatch_all_fail_closed() {
  const jint before = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedA, kActionDown, kInsideX, kInsideY, 1, 100);
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedA);
  const jint after = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedA, kActionUp, kInsideX, kInsideY, 1, 200);
  if (before != -1 || after != -1) {
    std::printf("  detail: expected -1/-1, got %d/%d\n",
                static_cast<int>(before), static_cast<int>(after));
    return false;
  }
  return true;
}

// Destroy must not arm a later dispatch: a full DOWN→UP tap on inside-button
// coordinates after destroy still resolves nothing (null env → -1 throughout;
// with a live VM this ordering is the destroy-then-use T7 half).
bool check_destroy_does_not_arm_later_dispatch() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedB);
  const jint down = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedB, kActionDown, kInsideX, kInsideY, 7, 300);
  const jint up = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedB, kActionUp, kInsideX, kInsideY, 7, 360);
  if (down != -1 || up != -1) {
    std::printf("  detail: expected -1/-1, got %d/%d\n", static_cast<int>(down),
                static_cast<int>(up));
    return false;
  }
  return true;
}

// Distinct forged handles stay isolated through destroy: destroying A then B
// leaves dispatches on both failing closed (no cross-talk via the registry).
bool check_distinct_forged_handles_isolated_through_destroy() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedA);
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, kForgedB);
  const jint a = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedA, kActionUp, kInsideX, kInsideY, 1, 400);
  const jint b = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, kForgedB, kActionUp, kInsideX, kInsideY, 2, 400);
  if (a != -1 || b != -1) {
    std::printf("  detail: expected -1/-1, got %d/%d\n", static_cast<int>(a),
                static_cast<int>(b));
    return false;
  }
  return true;
}

// Zero-handle destroy-then-use: destroy(0) no-ops, dispatch(0) yields -1.
bool check_zero_handle_destroy_then_use_fails_closed() {
  Java_com_muse_host_MainActivity_nativeDestroy(nullptr, nullptr, 0);
  const jint got = Java_com_muse_host_MainActivity_nativeDispatchTouch(
      nullptr, nullptr, 0, kActionUp, kInsideX, kInsideY, 0, 0);
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
  RUN_CASE(check_double_destroy_same_forged_handle_no_crash);
  RUN_CASE(check_double_destroy_zero_handle_no_crash);
  RUN_CASE(check_destroy_then_dispatch_fails_closed);
  RUN_CASE(check_dispatch_destroy_dispatch_all_fail_closed);
  RUN_CASE(check_destroy_does_not_arm_later_dispatch);
  RUN_CASE(check_distinct_forged_handles_isolated_through_destroy);
  RUN_CASE(check_zero_handle_destroy_then_use_fails_closed);
  if (failures > 0) {
    std::printf("gallery_registry_lifetime_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_registry_lifetime_test: all passing\n");
  return 0;
}
