#include "lists/collection_shape.h"

#include <cstddef>

namespace lists {
namespace {

// Unit column width: every frame satisfies w == span * unitW, so the
// smallest w/span ratio recovers the grid unit. The layout span count is
// then round(1 / unitW) — matching BuildLayout's colW = 1/spans.
bool InferGrid(const CollectionLayout& l, float& unitW, int& spans) {
  if (l.frames.empty()) return false;
  float minUnit = -1.0f;
  for (const auto& f : l.frames) {
    if (f.span < 1) continue;
    const float unit = f.w / static_cast<float>(f.span);
    if (unit <= 0.0f) continue;
    if (minUnit < 0.0f || unit < minUnit) minUnit = unit;
  }
  if (minUnit <= 0.0f) return false;
  unitW = minUnit;
  spans = static_cast<int>(1.0f / minUnit + 0.5f);
  if (spans < 1) spans = 1;
  return true;
}

}  // namespace

bool ReflowSpan(CollectionLayout& l, ItemKey k, int newSpan) {
  if (newSpan < 1) return false;
  std::size_t target = l.frames.size();
  for (std::size_t i = 0; i < l.frames.size(); ++i) {
    if (l.frames[i].key == k) {
      target = i;
      break;
    }
  }
  if (target == l.frames.size()) return false;

  float unitW = 0.0f;
  int spans = 0;
  if (!InferGrid(l, unitW, spans)) return false;
  if (newSpan > spans) return false;

  // Local re-layout: only the target cell's span/frame changes; all other
  // cells keep their keys, spans, and frames (no whole-list rebind).
  l.frames[target].span = newSpan;
  l.frames[target].w = static_cast<float>(newSpan) * unitW;
  return true;
}

bool VerifyShapeScenario(const ScenarioResult& r) {
  return r.jumpOk && r.reflowOk && r.stateSwitchOk;
}

}  // namespace lists
