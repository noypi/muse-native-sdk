// T3 contract tests — FR-LISTS-003 / AC-LISTS-009 (TC-LISTS-009).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps.
// Binds to the T3 `Interface:` line:
//   `void setOffset(ScrollSignal& sig, float px); float offset(const
//    ScrollSignal& sig); uint64_t layoutVersion(const ScrollSignal& sig);`
//   — setOffset notifies without bumping layoutVersion for unaffected
//   items.
// Include path follows the engine convention:
//   #include "lists/scroll_signal.h" with engine/lists/include on the path.
//
// ASSUMPTION (ScrollSignal construction): the plan/Interface line does not
// settle how a ScrollSignal is created. This test proposes default
// construction (`lists::ScrollSignal sig{};` starts at offset 0). If the
// coder names construction differently, update these call sites — not the
// expected behavior.
//
// ASSUMPTION (subscriber observation): the plan/Interface line names no
// subscribe function, so the render-graph subscriber is modelled as a
// poll: after every setOffset the test reads offset() back, exactly as a
// render-graph pull would. "Observes every offset" = every written value
// is returned in order with none dropped or reordered. If the coder adds a
// push-subscribe API, that is additive and these tests still hold.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "lists/scroll_signal.h"
#include "lists/stable_key.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

}  // namespace

int main() {
  // Consumes batch-1 T1: the window under test is keyed by stable keys;
  // proves the tester builds on the staged header without redefining it.
  {
    std::vector<lists::ItemKey> keys(2);
    keys[0].key = "row-0";
    keys[1].key = "row-1";
    lists::KeyCheck kc = lists::checkKeys(keys, false);
    check(kc.ok, "t1_window_keys_pass");
  }

  // Fresh signal starts at rest: offset 0.
  {
    lists::ScrollSignal sig{};
    check(lists::offset(sig) == 0.0f, "initial_offset_zero");
  }

  // setOffset stores; offset() reads back exactly.
  {
    lists::ScrollSignal sig{};
    lists::setOffset(sig, 250.0f);
    check(lists::offset(sig) == 250.0f, "offset_readback_exact");
  }

  // Render-graph subscriber (poll model) observes every offset in order
  // across a sweep — none dropped, none reordered.
  {
    lists::ScrollSignal sig{};
    const float kSweep[] = {0.0f,  16.0f,  33.0f,  64.0f,  128.0f,
                            256.0f, 512.0f, 768.0f, 1024.0f, 1536.0f};
    bool allObserved = true;
    for (float px : kSweep) {
      lists::setOffset(sig, px);
      if (lists::offset(sig) != px) allObserved = false;
    }
    check(allObserved, "subscriber_observes_every_offset");
  }

  // Sweeping offset leaves layoutVersion unchanged (pure offset change
  // relayouts nothing — in particular, never-visible items are untouched).
  {
    lists::ScrollSignal sig{};
    lists::setOffset(sig, 100.0f);
    uint64_t before = lists::layoutVersion(sig);
    const float kFrames[] = {120.0f, 200.0f, 350.0f, 500.0f,
                             750.0f, 900.0f, 1100.0f, 1400.0f};
    for (float px : kFrames) lists::setOffset(sig, px);
    check(lists::layoutVersion(sig) == before, "sweep_keeps_layout_version");
  }

  // layoutVersion is stable across a no-op (same-value) write too.
  {
    lists::ScrollSignal sig{};
    lists::setOffset(sig, 64.0f);
    uint64_t before = lists::layoutVersion(sig);
    lists::setOffset(sig, 64.0f);
    check(lists::layoutVersion(sig) == before, "same_value_no_relayout");
    check(lists::offset(sig) == 64.0f, "same_value_offset_kept");
  }

  // Boundary: negative overscroll offset is stored exactly (clamping is
  // the physics layer's job, not the signal's).
  {
    lists::ScrollSignal sig{};
    lists::setOffset(sig, -40.0f);
    check(lists::offset(sig) == -40.0f, "negative_offset_stored");
  }

  // Boundary: large offsets survive exactly (no truncation).
  {
    lists::ScrollSignal sig{};
    lists::setOffset(sig, 100000.0f);
    check(lists::offset(sig) == 100000.0f, "large_offset_exact");
  }

  if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
  return g_failures == 0 ? 0 : 1;
}
