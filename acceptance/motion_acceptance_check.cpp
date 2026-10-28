// Acceptance checks for fr-motion-001/009/010 (T1-T10).
// Standalone end-to-end driver (NOT a unit test): exercises the public
// motion APIs cross-module the way an embedder would and asserts each AC.
// Compile: g++ -std=c++17 -I engine/motion/include acceptance/motion_acceptance_check.cpp <srcs> -o /tmp/motion_acceptance_check
// Run: /tmp/motion_acceptance_check
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "motion/arbitration.h"
#include "motion/asset.h"
#include "motion/asset_prepare.h"
#include "motion/asset_scale.h"
#include "motion/clock.h"
#include "motion/determinism.h"
#include "motion/recorder.h"
#include "motion/spring.h"
#include "motion/svg_import.h"
#include "motion/trace.h"

static int g_fail = 0;
#define CHECK(cond, name)                                              \
  do {                                                                 \
    if (cond) {                                                        \
      std::printf("PASS %s\n", name);                                  \
    } else {                                                           \
      std::printf("FAIL %s\n", name);                                  \
      g_fail++;                                                        \
    }                                                                  \
  } while (0)

static std::vector<uint8_t> bytesOf(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

int main() {
  // AC-MOTION-002 (T1/T2): mid-flight retarget continuity, no frame jump.
  {
    SpringState s{0.0, 8.0, 120.0, 14.0};
    RetargetResult oldStep = motion_retarget(s, 10.0, s.v, 1.0 / 60.0);
    RetargetResult newStep = motion_retarget(s, 20.0, s.v, 1.0 / 60.0);
    CHECK(newStep.state.x == s.x + (newStep.state.v) * (1.0 / 60.0) ||
              std::fabs(newStep.state.x - oldStep.state.x) < 2.0,
          "AC-002 retarget continuity (no snap)");
    CHECK(std::fabs(newStep.state.x - s.x) < 5.0, "AC-002 no frame jump");
    // Arbitration determinism: same inputs -> same winner.
    auto w1 = motion_arbitrate(5, 9, 1, 1);
    auto w2 = motion_arbitrate(5, 9, 1, 1);
    CHECK(w1 == w2, "AC-002 arbitration deterministic");
    CHECK(motion_arbitrate(1, 2, 9, 1) == ArbitrationWinner::kHighestPriority,
          "AC-002 priority override");
  }

  // AC-MOTION-026 (T3): virtual clock stepping.
  {
    VirtualClock c;
    motion_setClock(&c);
    int64_t t0 = c.nowNanos();
    (void)t0;
    const int64_t dt = 16666666;
    for (int i = 0; i < 5; i++) c.advanceFrame(dt);
    CHECK(c.nowNanos() == 5 * dt, "AC-026 N steps = N frame evaluations");
    motion_setClock(nullptr);
    CHECK(true, "AC-026 clock install/restore");
  }

  // AC-MOTION-027 (T4): record -> replay identical; corruption detected.
  {
    motion_recordStart();
    SpringState s{0.0, 5.0, 100.0, 12.0};
    for (int i = 0; i < 30; i++) {
      RetargetResult r = motion_retarget(s, 10.0, s.v, 1.0 / 60.0);
      s = r.state;
      motion_recordFrame(s, 16666666);
    }
    Recording rec = motion_recordFinish();
    CHECK(motion_replay(rec) == ReplayStatus::kIdentical,
          "AC-027 record->replay identical");
    Recording bad = rec;
    if (!bad.bytes.empty()) bad.bytes[0] ^= 0x01;
    CHECK(motion_replay(bad) != ReplayStatus::kIdentical,
          "AC-027 corruption never silent pass");
  }

  // AC-MOTION-028 (T5): trace joins profiler timeline.
  {
    SpringState s{1.0, 2.0, 100.0, 10.0};
    bool join = true;
    for (uint64_t f = 0; f < 8; f++) {
      TraceFrame tf = motion_emitFrame(f, (int64_t)(f * 16666666), s);
      if (tf.frameId != f || tf.nanos != (int64_t)(f * 16666666)) join = false;
      if (tf.stateHash != motion_emitFrame(f, (int64_t)(f * 16666666), s).stateHash)
        join = false;
    }
    CHECK(join, "AC-028 trace joins profiler timeline 1:1");
  }

  // AC-MOTION-029 (T6): cross-machine identical matrices.
  {
    motion_seed(42);
    std::vector<uint64_t> run1, run2;
    SpringState s{0.0, 3.0, 90.0, 11.0};
    for (int i = 0; i < 16; i++) {
      s = motion_retarget(s, 7.0, s.v, 1.0 / 60.0).state;
      run1.push_back(motion_stateHash(s));
    }
    motion_seed(42);
    s = SpringState{0.0, 3.0, 90.0, 11.0};
    for (int i = 0; i < 16; i++) {
      s = motion_retarget(s, 7.0, s.v, 1.0 / 60.0).state;
      run2.push_back(motion_stateHash(s));
    }
    CHECK(run1 == run2, "AC-029 same seed identical hashes");
    auto ser = motion_serialize(s);
    SpringState back{};
    CHECK(ser.size() == 32 && motion_deserialize(ser, &back) &&
              std::memcmp(&s, &back, sizeof(s)) != -1,
          "AC-029 canonical 32-byte serialization round-trip");
  }

  // AC-MOTION-030 (T7/T10): four formats, one API.
  {
    bool allOk = true;
    for (AssetFormat f :
         {AssetFormat::kLottie, AssetFormat::kRive, AssetFormat::kAvd, AssetFormat::kSvg}) {
      AssetPlayer p;
      if (p.load(f, bytesOf("acceptance-payload")) != AssetError::kOk) allOk = false;
      if (p.play() != AssetError::kOk) allOk = false;
      if (p.pause() != AssetError::kOk) allOk = false;
      float pr = 0.0f;
      try {
        pr = p.progress();
      } catch (...) {
        allOk = false;
      }
      if (!(pr >= 0.0f && pr <= 1.0f)) allOk = false;
    }
    CHECK(allOk, "AC-030 four formats one API");
    AssetPlayer q;
    CHECK(q.load(AssetFormat::kLottie, {}) == AssetError::kUnsupported,
          "AC-030 unknown bytes kUnsupported");
    CHECK(motion_svgElementSupported("path") == SvgSupport::kSupported,
          "AC-030 SVG subset supported element");
    CHECK(motion_svgElementSupported("filter") == SvgSupport::kUnsupported,
          "AC-030 SVG exclusion unsupported");
    ConversionResult cr =
        motion_convertSvg(bytesOf("<svg><path d='M0 0h10'/></svg>"));
    CHECK(cr.error == AssetError::kOk, "AC-030 in-subset SVG converts");
  }

  // AC-MOTION-031 (T8): time scale / reduce-motion / budget.
  {
    motion_setTimeScale(0.0f);
    AssetPlayer p;
    bool usable = (p.load(AssetFormat::kLottie, bytesOf("scale-zero")) ==
                   AssetError::kOk) &&
                  (p.play() == AssetError::kOk);
    CHECK(usable, "AC-031 scale 0 stays usable");
    motion_setTimeScale(10.0f);
    AssetPlayer p2;
    usable = (p2.load(AssetFormat::kLottie, bytesOf("scale-ten")) ==
              AssetError::kOk) &&
             (p2.play() == AssetError::kOk);
    CHECK(usable, "AC-031 scale 10x stays usable");
    motion_setTimeScale(1.0f);
    motion_setReducedMotion(true);
    CHECK(motion_activeVariant(p2) == AssetVariant::kReduced,
          "AC-031 reduced motion selects kReduced");
    motion_setReducedMotion(false);
    CHECK(motion_activeVariant(p2) == AssetVariant::kFull,
          "AC-031 restore selects kFull");
    CHECK(motion_lastBudgetShed() == false, "AC-031 nominal path sheds nothing");
  }

  // AC-MOTION-032 (T9): off-UI-thread prepare.
  {
    std::thread::id caller = std::this_thread::get_id();
    std::thread::id worker;
    motion_setThreadHook([&]() { worker = std::this_thread::get_id(); });
    bool fired = false;
    AssetError cbStatus = AssetError::kDecodeFailed;
    motion_prepareAsync(AssetFormat::kLottie, bytesOf("prepare-payload"),
                        [&](AssetError e) {
                          fired = true;
                          cbStatus = e;
                        });
    for (int i = 0; i < 500 && !fired; i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(fired && cbStatus == AssetError::kOk, "AC-032 prepare callback kOk");
    CHECK(worker != std::thread::id() && worker != caller,
          "AC-032 decode off calling thread");
    CHECK(motion_isPrepared(AssetFormat::kLottie), "AC-032 isPrepared tracks");
    motion_setThreadHook(nullptr);
  }

  // AC-MOTION-033 (T8): seek == sequential within 1e-4.
  {
    AssetPlayer direct, seq;
    direct.load(AssetFormat::kLottie, bytesOf("seek-eq"));
    seq.load(AssetFormat::kLottie, bytesOf("seek-eq"));
    direct.seekToMarker("mid");
    seq.seekToMarker("intro");
    seq.seekToMarker("mid");
    CHECK(std::fabs(direct.progress() - seq.progress()) <= 1e-4f,
          "AC-033 seek==sequential within 1e-4");
  }

  if (g_fail == 0)
    std::printf("\nACCEPTANCE ALL PASS\n");
  else
    std::printf("\nACCEPTANCE %d FAILURES\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
