#pragma once

#include <cstdint>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Injected harness fake observing JS-bridge hops. The production native path
// takes no JS handle, so propagateNative never bumps this counter; count()
// stays flat across native propagation.
class JsCounter {
 public:
  JsCounter() = default;

  int count() const { return count_; }

 private:
  int count_ = 0;
};

// In-process VSYNC fake; the OS clock is never touched. Each tick() advances
// one VSYNC; elapsed() reports how many VSYNCs have elapsed.
class VsyncClock {
 public:
  VsyncClock() = default;

  void tick() { ++elapsed_; }
  int elapsed() const { return elapsed_; }

 private:
  int elapsed_ = 0;
};

// Observable outcome of one native frame propagation.
struct FrameStats {
  std::uint32_t jsHops = 0;
  std::uint32_t jniHops = 0;
  std::uint32_t vsyncsElapsed = 0;
};

// Delivers a coalesced native batch to the frame within one VSYNC tick. The
// production path takes no JS/JNI handle at all: jsHops and jniHops are
// always zero and the injected JsCounter is never touched. Values already
// committed in the store are left in place (last-writer-wins survives), as is
// each atom's provenance recording. A non-empty batch advances the clock by
// exactly one tick; an empty (drained) batch advances nothing.
FrameStats propagateNative(AtomStore& s, Batch b, JsCounter& js, VsyncClock& clk);

}  // namespace state
}  // namespace muse

// The contract tests call propagateNative unqualified. It takes AtomStore,
// Batch, JsCounter, and VsyncClock arguments, so it resolves via ADL; it is
// also surfaced at global scope to match the test-file spelling.
using muse::state::propagateNative;
