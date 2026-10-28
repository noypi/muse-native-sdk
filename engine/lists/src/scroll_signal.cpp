#include "lists/scroll_signal.h"

namespace lists {

void setOffset(ScrollSignal& sig, float px) { sig.offsetPx = px; }

float offset(const ScrollSignal& sig) { return sig.offsetPx; }

std::uint64_t layoutVersion(const ScrollSignal& sig) {
  return sig.layoutVersion;
}

}  // namespace lists
