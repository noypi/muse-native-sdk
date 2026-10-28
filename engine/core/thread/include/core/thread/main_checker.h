#pragma once

#include <cstddef>
#include <thread>

namespace core {
namespace thread {

// Debugbiuld diagnostic + release violation flag for Java-UI-touching calls.
//
// Contract (T5 / AC-CORE-023): any JNI call touching a Java UI object must
// execute on the main thread. Off-main use records a violation naming the
// call site, readable via violations()/lastWhat().
//
// Debug behaviour: the violation is always recorded (so hermetic unit tests
// can observe it); in debug builds a diagnostic is additionally printed to
// stderr. A hard trap (assert/abort) would make the contract untestable —
// the off-main unit test must survive to observe the flag — so no abort is
// issued here; device builds may enable a hard trap at their own seam.
//
// Host seam: main-thread identity is injected via setMainThreadId so tests
// stay hermetic (no JNI, no looper).
class MainThreadChecker {
 public:
  static void setMainThreadId(std::thread::id id) noexcept;
  static void check(const char* what) noexcept;
  static std::size_t violations() noexcept;
  static const char* lastWhat() noexcept;
  static void resetForTest() noexcept;

 private:
  MainThreadChecker() = delete;
};

// LocalRefScope RAII: Push/Pop analogue around JNI local references.
// Each live scope increments depth; destruction decrements it.
// Dispatcher wraps every callback in one scope (depth balanced per drain).
class LocalRefScope {
 public:
  LocalRefScope() noexcept;
  ~LocalRefScope() noexcept;
  LocalRefScope(const LocalRefScope&) = delete;
  LocalRefScope& operator=(const LocalRefScope&) = delete;

  static int depth() noexcept;
};

}  // namespace thread
}  // namespace core
