#include "muse/state/frame.hpp"

namespace muse {
namespace state {

FrameStats propagateNative(AtomStore& s, Batch b, JsCounter& js, VsyncClock& clk) {
  // Native delivery: no JS/JNI handle exists on this path, so neither the
  // injected counter nor the store contents are touched. Only the frame
  // clock advances, by at most one VSYNC tick.
  (void)s;
  (void)js;
  if (!b.empty()) {
    clk.tick();
  }
  FrameStats st;
  st.jsHops = 0;
  st.jniHops = 0;
  st.vsyncsElapsed = static_cast<std::uint32_t>(clk.elapsed());
  return st;
}

}  // namespace state
}  // namespace muse
