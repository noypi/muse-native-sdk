// Tester acceptance probe for fr-nav-005-fr-nav-002-fr-nav-004.
// Exercises AC behaviors end-to-end beyond the per-test binaries:
// real 4-level pop + modal push timed vs <4ms, predictive-back 60-event
// stream timing vs <1ms/event, cold-link zero-JS, bridge snapshot round
// trip, single-router bypass denial, T6 unknown-route home fallback,
// and the review-flagged T6 hardcoded-registry caveat (profile blob).
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/cold_link.h"
#include "navigation/graph.h"
#include "navigation/predictive_back.h"
#include "navigation/router_bridge.h"
#include "navigation/router_policy.h"
#include "navigation/state_schema.h"

static int failures = 0;
#define CHECK(cond, name)                                     \
  do {                                                        \
    if (!(cond)) { ++failures; std::printf("FAIL %s\n", name); } \
    else { std::printf("PASS %s\n", name); }                   \
  } while (0)

static NavOp Push(const std::string& r) { NavOp o; o.type = NavOpType::Push; o.routeId = r; return o; }

int main() {
  // T7-AC: real 4-level pop + modal push timed against <4ms.
  {
    NavGraph g({"home", "a", "b", "c", "d", "m1"});
    for (auto r : {"a", "b", "c", "d"}) g.apply(Push(r));
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; ++i) { NavOp p; p.type = NavOpType::Pop; g.apply(p); }
    auto t1 = std::chrono::steady_clock::now();
    NavOp m; m.type = NavOpType::PushModal; m.routeId = "m1";
    g.apply(m);
    auto t2 = std::chrono::steady_clock::now();
    long long popUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    long long modalUs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("INFO 4-level pop=%lldus modal_push=%lldus\n", popUs, modalUs);
    CHECK(popUs < 4000, "accept 4-level pop <4ms");
    CHECK(modalUs < 4000, "accept modal push <4ms");
    CHECK(g.serialize().find("m1") != std::string::npos, "accept modal visible after push");
  }
  // T3-AC: 60-event stream, per-event <1ms, progress before lift.
  {
    PredictiveBack pb;
    long long worst = 0;
    for (int i = 1; i <= 60; ++i) {
      auto t0 = std::chrono::steady_clock::now();
      pb.onProgress(BackProgress{static_cast<float>(i) / 60.0f, 0, false});
      auto t1 = std::chrono::steady_clock::now();
      long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      if (us > worst) worst = us;
    }
    std::printf("INFO worst per-event=%lldus final=%f\n", worst, (double)pb.transitionProgress());
    CHECK(worst < 1000, "accept per-event <1ms");
    CHECK(pb.transitionProgress() > 0.99f, "accept progress visible before lift");
  }
  // T7-AC: cold link zero-JS + parent chain.
  {
    ColdLinkResult r = resolveColdLink("muse://details/42");
    CHECK(r.ok && !r.jsExecuted, "accept cold link zero-JS");
    CHECK(r.stack.graphBlob.find("home") != std::string::npos &&
          r.stack.graphBlob.find("details") != std::string::npos,
          "accept cold link parent chain");
  }
  // T4-AC: bridge navigate/back observable via snapshot + round trip.
  {
    RouterBridge b({"home", "details", "settings"});
    auto r1b = b.invoke("navigate", "{\"route\":\"details\"}");
    CHECK((r1b.ok), "accept bridge navigate ok");
    CHECK((b.snapshotForPersist().find("details") != std::string::npos), "accept bridge snapshot shows nav");
    auto r2b = b.invoke("back", "{}");
    CHECK((r2b.ok), "accept bridge back ok");
    auto badb = b.invoke("nope", "{}");
    CHECK((!badb.ok && badb.error == BridgeError::UNKNOWN_METHOD), "accept bridge unknown method typed");
    std::string snap = b.snapshotForPersist();
    RouterBridge b2({"home", "details", "settings"});
    auto r3b = b2.invoke("navigate", "{\"route\":\"settings\"}");
    CHECK((r3b.ok), "accept bridge second navigate ok");
    CHECK((b2.restoreFromLifeSave(snap)), "accept LIFE-SAVE restore ok");
  }
  // T5-AC: bypass denied, no mutation.
  {
    RouterPolicy p({"home", "details"});
    CHECK(p.checkDestination("details").allowed, "accept registered allowed");
    auto v = p.checkDestination("activity/com.evil.Main");
    CHECK(!v.allowed && v.error == PolicyError::BYPASS_ATTEMPT, "accept activity bypass denied");
    CHECK(p.checkDestination("details").allowed, "accept no mutation after denial");
  }
  // T1-AC: one serialization format round trip.
  {
    NavGraph g({"home", "details"});
    g.apply(Push("details"));
    std::string blob = g.serialize();
    NavGraph h({"home", "details"});
    CHECK(h.restore(blob).ok && h.serialize() == blob, "accept serialize/restore round trip");
  }
  // T6-AC: unknown route -> home fallback, never crash.
  {
    NavGraph gt({"home", "details"});
    gt.apply(Push("details"));
    std::string tampered = gt.serialize();
    std::string::size_type pos = tampered.find("details");
    if (pos != std::string::npos) tampered.replace(pos, 7, "gone-route");
    NavSnapshot s; s.version = kNavStateVersion; s.graphBlob = tampered;
    EncodeResult e = encodeState(s);
    DecodeResult d = decodeState(e.blob.empty() ? tampered : e.blob);
    CHECK(d.fellBackToHome, "accept unknown route home fallback");
    DecodeResult c = decodeState("###corrupt###");
    CHECK(!c.ok && c.error == DecodeError::CORRUPT, "accept corrupt typed error");
  }
  // Review-flagged caveat probe: legitimate T1 route outside T6 table.
  {
    NavGraph g({"home", "profile"});
    g.apply(Push("profile"));
    NavSnapshot s2; s2.version = kNavStateVersion; s2.graphBlob = g.serialize();
    EncodeResult e2 = encodeState(s2);
    DecodeResult d2 = decodeState(e2.blob);
    std::printf("INFO profile-blob fellBackToHome=%d err=%d\n", (int)d2.fellBackToHome, (int)d2.error);
  }
  std::printf(failures == 0 ? "ACCEPT-PROBE ALL PASS\n" : "ACCEPT-PROBE FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}
