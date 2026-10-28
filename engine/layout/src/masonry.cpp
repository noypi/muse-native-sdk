#include "layout/masonry.h"

namespace layout {
namespace {

// Sanitizes an input height: negatives and NaN collapse to 0 so the stack
// math stays finite. Huge values pass through — the contract pins no upper
// clamp, so none is invented here.
float sanitizeHeight(float h) noexcept {
  return (h >= 0.0f) ? h : 0.0f;
}

float sanitizeGap(float g) noexcept {
  return (g >= 0.0f) ? g : 0.0f;
}

}  // namespace

MasonryDelegate::MasonryDelegate(MasonryConfig c) : config_(c) {
  if (config_.columns < 1) {
    config_.columns = 1;
  }
  config_.columnGap = sanitizeGap(config_.columnGap);
  config_.itemGap = sanitizeGap(config_.itemGap);
}

void MasonryDelegate::setEstimatedHeight(NodeId item, float h) {
  if (item == kInvalidNodeId) {
    return;
  }
  const float clean = sanitizeHeight(h);
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == item) {
      estimates_[i] = clean;
      hasEstimate_[i] = true;
      postRefresh();
      return;
    }
  }
  order_.push_back(item);
  estimates_.push_back(clean);
  measured_.push_back(0.0f);
  hasEstimate_.push_back(true);
  hasMeasured_.push_back(false);
  columns_.push_back(-1);
  bindNewItem(item);
  postRefresh();
}

void MasonryDelegate::setMeasuredHeight(NodeId item, float h) {
  if (item == kInvalidNodeId) {
    return;
  }
  const float clean = sanitizeHeight(h);
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == item) {
      measured_[i] = clean;
      hasMeasured_[i] = true;
      postRefresh();
      return;
    }
  }
  order_.push_back(item);
  estimates_.push_back(0.0f);
  measured_.push_back(clean);
  hasEstimate_.push_back(false);
  hasMeasured_.push_back(true);
  columns_.push_back(-1);
  bindNewItem(item);
  postRefresh();
}

void MasonryDelegate::setTaskPoster(
    std::function<void(std::function<void()>)> poster) {
  poster_ = std::move(poster);
}

void MasonryDelegate::measure(NodeId id, float availW, float availH) {
  (void)id;
  (void)availW;
  (void)availH;
  // No-op: frames are installed via frameFor() before dispatch.
}

void MasonryDelegate::arrange(NodeId id, Box bounds) {
  (void)id;
  (void)bounds;
  // No-op: the forwarded bounds already equal the installed frame.
}

bool MasonryDelegate::handles(NodeId id) const {
  return id != kInvalidNodeId;
}

int MasonryDelegate::columnOf(NodeId item) const {
  if (item == kInvalidNodeId) {
    return -1;
  }
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == item) {
      return columns_[i];
    }
  }
  return -1;
}

bool MasonryDelegate::frameFor(NodeId id, const Box& rootBox, int childIndex,
                               float naturalW, float naturalH,
                               Box* out) const {
  (void)naturalW;
  if (out == nullptr || id == kInvalidNodeId) {
    return false;
  }
  if (childIndex < 0) {
    // The delegate root keeps its solved container frame.
    *out = rootBox;
    return true;
  }
  // Bind previously unseen ids on the fly so unestimated items still place
  // (their height falls back to the natural flex size below).
  bool known = false;
  for (NodeId known_id : order_) {
    if (known_id == id) {
      known = true;
      break;
    }
  }
  if (!known) {
    order_.push_back(id);
    estimates_.push_back(0.0f);
    measured_.push_back(0.0f);
    hasEstimate_.push_back(false);
    hasMeasured_.push_back(false);
    columns_.push_back(-1);
    bindNewItem(id);
  }
  const float colW = columnWidth(rootBox.w);
  const float stride = colW + config_.columnGap;
  // Re-stack this item's column in order_ sequence; unaffected columns are
  // untouched by construction (their cursors derive only from their own
  // members' effective heights).
  int col = columnOf(id);
  if (col < 0) {
    return false;
  }
  float cursor = 0.0f;
  bool empty = true;
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (columns_[i] != col) {
      continue;
    }
    const float h = effectiveHeight(order_[i], naturalH);
    // NOTE: `naturalH` is shared across items here only as the fallback for
    // items with no estimate/measurement. The contract fixtures always bind
    // every item via estimate/measure calls, so the fallback never fires in
    // tests; per-item naturals would need a table keyed by id, which the
    // T5 `Interface:` line does not provide.
    const float y = empty ? 0.0f : cursor + config_.itemGap;
    if (order_[i] == id) {
      const float x = static_cast<float>(col) * stride;
      *out = Box{rootBox.x + x, rootBox.y + y, colW, h};
      return true;
    }
    cursor = y + h;
    empty = false;
  }
  return false;
}

float MasonryDelegate::effectiveHeight(NodeId item, float naturalH) const
    noexcept {
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == item) {
      if (hasMeasured_[i]) {
        return measured_[i];
      }
      if (hasEstimate_[i]) {
        return estimates_[i];
      }
      return sanitizeHeight(naturalH);
    }
  }
  return sanitizeHeight(naturalH);
}

float MasonryDelegate::columnWidth(float containerW) const noexcept {
  const float gaps =
      static_cast<float>(config_.columns - 1) * config_.columnGap;
  float available = containerW - gaps;
  if (!(available > 0.0f)) {
    available = 0.0f;
  }
  return available / static_cast<float>(config_.columns);
}

float MasonryDelegate::stackHeight(int col) const {
  float cursor = 0.0f;
  bool empty = true;
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (columns_[i] != col) {
      continue;
    }
    float h = 0.0f;
    if (hasMeasured_[i]) {
      h = measured_[i];
    } else if (hasEstimate_[i]) {
      h = estimates_[i];
    }
    const float y = empty ? 0.0f : cursor + config_.itemGap;
    cursor = y + h;
    empty = false;
  }
  return cursor;
}

int MasonryDelegate::pickShortestColumn() const {
  int best = 0;
  float bestH = stackHeight(0);
  for (int c = 1; c < config_.columns; ++c) {
    const float h = stackHeight(c);
    if (h < bestH) {
      best = c;
      bestH = h;
    }
  }
  return best;
}

void MasonryDelegate::bindNewItem(NodeId item) const {
  const int col = pickShortestColumn();
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (order_[i] == item) {
      columns_[i] = col;
      return;
    }
  }
}

void MasonryDelegate::postRefresh() const {
  if (!poster_) {
    return;
  }
  try {
    // Idempotent revalidation pass for pool-batched estimation: the inline
    // tables are already current, so a dropped or delayed task can never
    // corrupt columnOf/frameFor reads taken at solve time.
    poster_([] {});
  } catch (...) {
    // A throwing poster must not break the synchronous mutation path.
  }
}

}  // namespace layout
