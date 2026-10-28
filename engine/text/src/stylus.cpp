// FR-TEXT-004 / AC-TEXT-007: stylus sample batch expansion.
// History + anchor + predicted ordering with per-sample copies. Pure C++,
// no JNI types; the hot path performs a single reserve plus copies.

#include "muse/text/stylus.h"

namespace muse::text {

std::vector<StylusSample> ExpandSamples(
    const StylusSample& anchor, const std::vector<StylusSample>& history,
    const std::vector<StylusSample>& predicted) {
  std::vector<StylusSample> out;
  out.reserve(history.size() + 1 + predicted.size());
  out.insert(out.end(), history.begin(), history.end());
  out.push_back(anchor);
  out.insert(out.end(), predicted.begin(), predicted.end());
  return out;
}

}  // namespace muse::text
