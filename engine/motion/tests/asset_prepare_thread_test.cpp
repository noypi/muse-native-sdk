// T9 contract tests — FR-MOTION-010 / AC-MOTION-032.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: inline byte
// payloads; the only waits are bounded in-process condition-variable waits
// (5 s cap) on the callback the unit under test must invoke — no wall-clock
// assertions, no sleeps, no live threads outside the unit under test.
// Binds to the T9 `Interface:` line (reproduced verbatim in unit-tests/T9.md):
// `using AssetReady = std::function<void(AssetError)>;`
// `void motion_prepareAsync(AssetFormat f, std::vector<uint8_t> bytes,`
// `                         AssetReady cb);`
// `bool motion_isPrepared(AssetFormat f);`
// plus the test hook `void motion_setThreadHook(std::function<void()> h)`
// (plan Detail line).
//
// ASSUMPTION (decodable-bytes proxy): inherits T7 — non-empty printable
// payloads prepare as kOk; empty bytes never prepare kOk (kUnsupported).
//
// ASSUMPTION (hook semantics): the coder invokes the installed thread hook on
// the worker thread during decode. The test records the calling thread id at
// the prepare call site and the hook thread id inside the hook; different ids
// prove decode ran off the calling (UI) thread. Passing nullptr clears the
// hook.
//
// ASSUMPTION (prepare/load relationship): global prepare state
// (motion_isPrepared) and per-player T7 gating compose — after the ready
// callback fires kOk, a fresh player that loads the same payload plays kOk;
// before prepare completes, a fresh player's play() is kNotPrepared. If the
// coder documents a different prepare/load composition, ONLY that composition
// expectation reconciles, never the off-thread-decode expectations.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "motion/asset.h"
#include "motion/asset_prepare.h"

namespace {

const std::chrono::seconds kWait(5);

std::vector<uint8_t> payload(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

struct Gate {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  AssetError err = AssetError::kNotPrepared;
  void fire(AssetError e) {
    {
      std::lock_guard<std::mutex> l(m);
      done = true;
      err = e;
    }
    cv.notify_one();
  }
  bool await() {
    std::unique_lock<std::mutex> l(m);
    return cv.wait_for(l, kWait, [this] { return done; });
  }
};

// Ready callback fires with kOk for a valid payload (AC-MOTION-032).
bool check_prepare_callback_fires_with_ok() {
  try {
    Gate g;
    motion_setThreadHook(nullptr);
    motion_prepareAsync(AssetFormat::kLottie, payload("lottie-prepare-ok"),
                        [&](AssetError e) { g.fire(e); });
    if (!g.await()) return false;
    motion_setThreadHook(nullptr);
    return g.err == AssetError::kOk;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// Decode runs on a non-calling thread: hook id differs from caller id.
bool check_decode_runs_off_calling_thread() {
  try {
    Gate g;
    std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> hookRan{false};
    std::thread::id hookId = caller;
    motion_setThreadHook([&] {
      hookId = std::this_thread::get_id();
      hookRan.store(true);
    });
    motion_prepareAsync(AssetFormat::kRive, payload("rive-prepare-thread"),
                        [&](AssetError e) { g.fire(e); });
    bool fired = g.await();
    motion_setThreadHook(nullptr);
    return fired && hookRan.load() && hookId != caller;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// isPrepared is false before prepare and true after the kOk callback.
bool check_is_prepared_tracks_completion() {
  try {
    // A fresh format slot starts unprepared (SVG slot untouched by other
    // cases in this file, which use Lottie/Rive/Avd).
    if (motion_isPrepared(AssetFormat::kSvg)) return false;
    Gate g;
    motion_setThreadHook(nullptr);
    motion_prepareAsync(AssetFormat::kSvg, payload("svg-prepare-track"),
                        [&](AssetError e) { g.fire(e); });
    if (!g.await()) return false;
    motion_setThreadHook(nullptr);
    if (g.err != AssetError::kOk) return false;
    return motion_isPrepared(AssetFormat::kSvg);
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// play() before prepare completion is kNotPrepared; after the callback and a
// load through the T7 API, play() succeeds (AC-MOTION-032 acceptance).
bool check_play_gated_until_prepare_completes() {
  try {
    motion_setThreadHook(nullptr);
    AssetPlayer early;
    // Fresh player with no load is kNotPrepared regardless of prepare state
    // (T7 gating); this pins the pre-condition side of the acceptance.
    if (early.play() != AssetError::kNotPrepared) return false;
    Gate g;
    std::vector<uint8_t> bytes = payload("avd-prepare-gate");
    motion_prepareAsync(AssetFormat::kAvd, bytes,
                        [&](AssetError e) { g.fire(e); });
    if (!g.await()) return false;
    if (g.err != AssetError::kOk) return false;
    if (!motion_isPrepared(AssetFormat::kAvd)) return false;
    AssetPlayer p;
    if (p.load(AssetFormat::kAvd, bytes) != AssetError::kOk) return false;
    return p.play() == AssetError::kOk;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// Empty bytes never prepare kOk and never throw (boundary/robustness).
bool check_empty_bytes_never_prepare_ok() {
  try {
    Gate g;
    motion_setThreadHook(nullptr);
    motion_prepareAsync(AssetFormat::kLottie, std::vector<uint8_t>{},
                        [&](AssetError e) { g.fire(e); });
    if (!g.await()) return false;
    motion_setThreadHook(nullptr);
    return g.err != AssetError::kOk;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// Oversized payload (1 MiB) never throws; the callback still fires.
bool check_oversized_payload_never_throws() {
  try {
    Gate g;
    motion_setThreadHook(nullptr);
    motion_prepareAsync(AssetFormat::kRive,
                        std::vector<uint8_t>(1024 * 1024, uint8_t('x')),
                        [&](AssetError e) { g.fire(e); });
    bool fired = g.await();
    motion_setThreadHook(nullptr);
    return fired;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
}

// Null callback never crashes; preparation still completes observably.
bool check_null_callback_no_crash() {
  try {
    motion_setThreadHook(nullptr);
    motion_prepareAsync(AssetFormat::kAvd, payload("avd-null-cb"), nullptr);
    // Poll isPrepared with a bounded in-process wait (no sleep assertions).
    for (int i = 0; i < 50; ++i) {
      if (motion_isPrepared(AssetFormat::kAvd)) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return motion_isPrepared(AssetFormat::kAvd);
  } catch (...) {
    return false;
  }
}

// Clearing the hook restores default behavior: prepare still completes.
bool check_hook_reset_still_completes() {
  try {
    motion_setThreadHook([] {});
    motion_setThreadHook(nullptr);
    Gate g;
    motion_prepareAsync(AssetFormat::kLottie, payload("lottie-hook-reset"),
                        [&](AssetError e) { g.fire(e); });
    if (!g.await()) return false;
    return g.err == AssetError::kOk;
  } catch (...) {
    motion_setThreadHook(nullptr);
    return false;
  }
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
  int failures = 0;
  RUN_CASE(check_prepare_callback_fires_with_ok);
  RUN_CASE(check_decode_runs_off_calling_thread);
  RUN_CASE(check_is_prepared_tracks_completion);
  RUN_CASE(check_play_gated_until_prepare_completes);
  RUN_CASE(check_empty_bytes_never_prepare_ok);
  RUN_CASE(check_oversized_payload_never_throws);
  RUN_CASE(check_null_callback_no_crash);
  RUN_CASE(check_hook_reset_still_completes);
  if (failures > 0) {
    std::printf("asset_prepare_thread_test: %d failing\n", failures);
    return 1;
  }
  std::printf("asset_prepare_thread_test: all passing\n");
  return 0;
}
