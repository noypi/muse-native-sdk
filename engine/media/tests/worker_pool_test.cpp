// Batch 1 — worker-pool decode dispatch (NFR-MEDIA-001/AC-MEDIA-001, TC-MEDIA-001).
// Binds ONLY to the T2 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + read-only core/graphics headers and the new
// engine/media/decode/decode_dispatch.h only; no network, no env/config
// reads, no filesystem, no live services. Fixtures are inline byte arrays.
//
// Expected state: RED — engine/media/decode/decode_dispatch.h(.cc) do not
// exist yet, so this file fails to compile until the coder implements T2
// (T1 scaffolding alone must NOT fake these behaviors green).
//
// Contract assumptions (see unit-tests/T2.md):
//   A-NS:  the header declares everything in namespace `media`.
//   A-REQ: `media::DecodeRequest` is default-constructible and carries at
//          least `core::thread::CancellationToken tok` (cooperative cancel),
//          `core::budget::Manager* budget` (non-null VRAM accounting source),
//          and `size_t vramBytes` (bytes to reserve for the decoded image).
//   A-OUT: `media::DecodedImage` is default-constructible with
//          `bool uploaded == false`, set to true by the worker after a
//          successful `poolAcquire` upload, never touched on reject/cancel.
//   A-OK:  success outcome is `{true, Status::kOk}`.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

#include "core/budget/manager.h"
#include "core/thread/task.h"
#include "core/thread/worker_pool.h"
#include "engine/media/decode/decode_dispatch.h"

// --- tiny harness (matches engine/core/thread/tests convention) -------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// Wait up to ~5s for a predicate (poll-based; never asserts exact timing
// except the plan-mandated 50 ms dispatch bound in check_dispatch_returns).
template <typename Pred>
static bool wait_for(Pred pred) {
  for (int i = 0; i < 500 && !pred(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}

// --- fixtures ---------------------------------------------------------------
// Minimal fake JPEG-ish header bytes (SOI + APP0); the T2 loop never links a
// real codec (plan A3) — these stand in for caller-supplied compressed bytes.
static const uint8_t kFakeJpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                                    0x4A, 0x46, 0x49, 0x46, 0x00, 0x01};
static const std::size_t kFakeJpegLen = sizeof(kFakeJpeg);

static media::DecodeRequest MakeReq(core::budget::Manager* budget,
                                   std::size_t vramBytes) {
  media::DecodeRequest req;
  req.budget = budget;
  req.vramBytes = vramBytes;
  return req;
}

// Occupy every worker of a pool(2) with higher-priority (Render) blockers so
// subsequently submitted Decode tasks stay queued behind `gate`.
static std::promise<void> OccupyWorkers(core::thread::WorkerPool& pool) {
  std::promise<void> gate;
  auto gate_future = gate.get_future();
  auto shared_gate = std::make_shared<std::future<void>>(std::move(gate_future));
  for (int i = 0; i < 2; ++i) {
    core::thread::Task blocker;
    blocker.pri = core::thread::Priority::Render;
    blocker.fn = [shared_gate] { shared_gate->wait(); };
    pool.submit(std::move(blocker));
  }
  return gate;
}

// --- cases ------------------------------------------------------------------

// InvalidArg shapes: nullptr bytes / zero length / nullptr out are rejected
// synchronously without touching the pool.
static void check_invalid_arg_shapes() {
  core::thread::WorkerPool pool(1);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 1ULL << 20});
  media::DecodeRequest req = MakeReq(&budget, 64);
  media::DecodedImage out;

  media::MediaDecodeOutcome o1 =
      media::dispatchDecode(pool, &out, nullptr, kFakeJpegLen, req);
  CHECK("null bytes rejected", o1.accepted == false);
  CHECK("null bytes status kInvalidArg",
        o1.status == media::Status::kInvalidArg);

  media::MediaDecodeOutcome o2 =
      media::dispatchDecode(pool, &out, kFakeJpeg, 0, req);
  CHECK("zero length rejected", o2.accepted == false);
  CHECK("zero length status kInvalidArg",
        o2.status == media::Status::kInvalidArg);

  media::MediaDecodeOutcome o3 =
      media::dispatchDecode(pool, nullptr, kFakeJpeg, kFakeJpegLen, req);
  CHECK("null out rejected", o3.accepted == false);
  CHECK("null out status kInvalidArg",
        o3.status == media::Status::kInvalidArg);

  pool.shutdown();
}

// OverBudget shape: exhausted VRAM budget rejects synchronously.
static void check_over_budget_shape() {
  core::thread::WorkerPool pool(1);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 128});
  CHECK("setup exhausted test budget", budget.acquireVram(128));
  media::DecodeRequest req = MakeReq(&budget, 64);
  media::DecodedImage out;

  media::MediaDecodeOutcome o =
      media::dispatchDecode(pool, &out, kFakeJpeg, kFakeJpegLen, req);
  CHECK("over-budget not accepted", o.accepted == false);
  CHECK("over-budget status kOverBudget",
        o.status == media::Status::kOverBudget);
  CHECK("over-budget leaves out untouched", out.uploaded == false);

  pool.shutdown();
}

// Non-blocking dispatch (AC-MEDIA-001 core): while both workers are gated,
// dispatchDecode returns within 50 ms — proving no inline decode on the
// caller (render) thread.
static void check_dispatch_returns_while_gated() {
  core::thread::WorkerPool pool(2);
  std::promise<void> gate = OccupyWorkers(pool);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 1ULL << 20});
  media::DecodeRequest req = MakeReq(&budget, 64);
  media::DecodedImage out;

  const auto t0 = std::chrono::steady_clock::now();
  media::MediaDecodeOutcome o =
      media::dispatchDecode(pool, &out, kFakeJpeg, kFakeJpegLen, req);
  const auto elapsed = std::chrono::steady_clock::now() - t0;

  CHECK("dispatch accepted while gated", o.accepted == true);
  CHECK("dispatch success status kOk", o.status == media::Status::kOk);
  CHECK("dispatch returns within 50ms while workers gated",
        elapsed < std::chrono::milliseconds(50));

  gate.set_value();
  CHECK("gated decode completes after release",
        wait_for([&out] { return out.uploaded; }));
  pool.shutdown();
}

// Accepted decodes all complete once the gate releases (N=4).
static void check_accepted_decodes_complete() {
  core::thread::WorkerPool pool(2);
  std::promise<void> gate = OccupyWorkers(pool);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 1ULL << 20});
  constexpr int kN = 4;
  std::vector<media::DecodedImage> outs(kN);
  std::vector<media::DecodeRequest> reqs;
  for (int i = 0; i < kN; ++i) reqs.push_back(MakeReq(&budget, 64));

  int accepted = 0;
  for (int i = 0; i < kN; ++i) {
    media::MediaDecodeOutcome o = media::dispatchDecode(
        pool, &outs[i], kFakeJpeg, kFakeJpegLen, reqs[i]);
    if (o.accepted) ++accepted;
  }
  CHECK("all N decodes accepted", accepted == kN);
  gate.set_value();
  CHECK("every accepted decode completes",
        wait_for([&outs] {
          for (const auto& o : outs)
            if (!o.uploaded) return false;
          return true;
        }));
  pool.shutdown();
}

// Cooperative cancellation (Task::tok): a decode cancelled while queued
// observes isCancelled() and skips upload — no crash, no leak, no upload.
static void check_cancelled_skips_upload() {
  core::thread::WorkerPool pool(1);
  std::promise<void> gate = OccupyWorkers(pool);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 1ULL << 20});
  media::DecodeRequest cancelled_req = MakeReq(&budget, 64);
  media::DecodeRequest live_req = MakeReq(&budget, 64);
  media::DecodedImage cancelled_out;
  media::DecodedImage live_out;

  // Queue the victim first, then cancel before it can run.
  media::MediaDecodeOutcome ov = media::dispatchDecode(
      pool, &cancelled_out, kFakeJpeg, kFakeJpegLen, cancelled_req);
  (void)ov;
  cancelled_req.tok.cancel();
  media::MediaDecodeOutcome ol = media::dispatchDecode(
      pool, &live_out, kFakeJpeg, kFakeJpegLen, live_req);
  (void)ol;

  // Release the worker: the queued second Render blocker passes straight
  // through the already-satisfied gate, then victim and sibling run in order.
  gate.set_value();
  CHECK("live sibling completes",
        wait_for([&live_out] { return live_out.uploaded; }));
  // Give the worker a chance to (incorrectly) upload the cancelled decode.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK("cancelled decode skipped upload", !cancelled_out.uploaded);
  pool.shutdown();  // clean join proves no leak/hang
}

// Input validation (security): malformed bytes never throw and never crash;
// the outcome is a well-formed shape (accepted or a rejection).
static void check_malformed_bytes_never_throw() {
  core::thread::WorkerPool pool(2);
  core::budget::Manager budget(
      core::budget::Manager::Config{8ULL << 30, 1ULL << 20});
  media::DecodeRequest req = MakeReq(&budget, 64);
  media::DecodedImage out;
  static const uint8_t kGarbage[] = {0x00, 0xFF, 0x41, 0x42, 0x7F};
  bool threw = false;
  media::MediaDecodeOutcome o{false, media::Status::kInvalidArg};
  try {
    o = media::dispatchDecode(pool, &out, kGarbage, sizeof(kGarbage), req);
  } catch (...) {
    threw = true;
  }
  CHECK("malformed bytes never throw", !threw);
  CHECK("malformed outcome is a valid shape",
        (o.accepted && o.status == media::Status::kOk) ||
            (!o.accepted &&
             (o.status == media::Status::kInvalidArg ||
              o.status == media::Status::kOverBudget)));
  pool.shutdown();
}

int main() {
  check_invalid_arg_shapes();
  check_over_budget_shape();
  check_dispatch_returns_while_gated();
  check_accepted_decodes_complete();
  check_cancelled_skips_upload();
  check_malformed_bytes_never_throw();
  if (s_failures == 0) {
    std::printf("ALL GREEN worker_pool_test\n");
    return 0;
  }
  std::printf("%d FAILURES worker_pool_test\n", s_failures);
  return 1;
}
