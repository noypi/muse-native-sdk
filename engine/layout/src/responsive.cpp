#include "responsive.h"

#include <cmath>

namespace layout {

Node makeGrid(int columns, float cellW, float cellH, int childCount) noexcept {
  Node n;
  n.kind = Node::Kind::kGrid;
  n.columns = columns;
  n.cellW = cellW;
  n.cellH = cellH;
  n.childCount = childCount;
  return n;
}

Node makeAspectBox(float ratio, float width) noexcept {
  Node n;
  n.kind = Node::Kind::kAspectBox;
  n.ratio = ratio;
  n.width = width;
  return n;
}

Node makeWrapRow(float childW, float childH, int childCount) noexcept {
  Node n;
  n.kind = Node::Kind::kWrapRow;
  n.childW = childW;
  n.childH = childH;
  n.childCount = childCount;
  return n;
}

Node makeBaselineBox(float width, float height, float baseline) noexcept {
  Node n;
  n.kind = Node::Kind::kBaselineBox;
  n.width = width;
  n.height = height;
  n.baseline = baseline;
  return n;
}

Node makeUnsupported(int bogusKind) noexcept {
  Node n;
  n.kind = Node::Kind::kUnsupported;
  n.bogusKind = bogusKind;
  return n;
}

namespace {

LayoutResult invalid() noexcept {
  LayoutResult r;
  r.size.width = 0.0f;
  r.size.height = 0.0f;
  r.baseline = 0.0f;
  r.status = Status::kInvalidMetrics;
  return r;
}

LayoutResult unsupported() noexcept {
  LayoutResult r;
  r.size.width = 0.0f;
  r.size.height = 0.0f;
  r.baseline = 0.0f;
  r.status = Status::kUnsupportedProperty;
  return r;
}

LayoutResult measureGrid(const Node& n) noexcept {
  if (n.columns <= 0 || n.childCount < 0 || !(n.cellW >= 0.0f) ||
      !(n.cellH >= 0.0f) || !std::isfinite(n.cellW) ||
      !std::isfinite(n.cellH)) {
    return invalid();
  }
  if (n.childCount == 0) {
    LayoutResult r;
    r.size.width = 0.0f;
    r.size.height = 0.0f;
    r.baseline = 0.0f;
    r.status = Status::kOk;
    return r;
  }
  const int rows = (n.childCount + n.columns - 1) / n.columns;
  LayoutResult r;
  r.size.width = static_cast<float>(n.columns) * n.cellW;
  r.size.height = static_cast<float>(rows) * n.cellH;
  r.baseline = 0.0f;
  r.status = Status::kOk;
  return r;
}

LayoutResult measureAspectBox(const Node& n) noexcept {
  if (!(n.ratio > 0.0f) || !std::isfinite(n.ratio) || !(n.width >= 0.0f) ||
      !std::isfinite(n.width)) {
    return invalid();
  }
  LayoutResult r;
  r.size.width = n.width;
  r.size.height = n.width / n.ratio;
  r.baseline = 0.0f;
  r.status = Status::kOk;
  return r;
}

LayoutResult measureWrapRow(const Node& n, Constraints c) noexcept {
  if (!(n.childW > 0.0f) || !std::isfinite(n.childW) || !(n.childH >= 0.0f) ||
      !std::isfinite(n.childH) || n.childCount < 0) {
    return invalid();
  }
  if (n.childCount == 0) {
    LayoutResult r;
    r.status = Status::kOk;
    return r;
  }
  int perLine = 1;
  if (c.maxWidth > 0.0f && std::isfinite(c.maxWidth)) {
    perLine = static_cast<int>(c.maxWidth / n.childW);
    if (perLine < 1) {
      perLine = 1;
    }
  }
  const int lines = (n.childCount + perLine - 1) / perLine;
  const int firstLineCount = n.childCount < perLine ? n.childCount : perLine;
  LayoutResult r;
  r.size.width = static_cast<float>(firstLineCount) * n.childW;
  r.size.height = static_cast<float>(lines) * n.childH;
  r.baseline = 0.0f;
  r.status = Status::kOk;
  return r;
}

LayoutResult measureBaselineBox(const Node& n) noexcept {
  if (!(n.width >= 0.0f) || !(n.height >= 0.0f) ||
      !std::isfinite(n.width) || !std::isfinite(n.height) ||
      !std::isfinite(n.baseline) || n.baseline < 0.0f ||
      n.baseline > n.height) {
    return invalid();
  }
  LayoutResult r;
  r.size.width = n.width;
  r.size.height = n.height;
  r.baseline = n.baseline;
  r.status = Status::kOk;
  return r;
}

}  // namespace

LayoutResult measureResponsive(Node n, Constraints c) noexcept {
  switch (n.kind) {
    case Node::Kind::kEmpty: {
      LayoutResult r;
      r.size.width = 0.0f;
      r.size.height = 0.0f;
      r.baseline = 0.0f;
      r.status = Status::kOk;
      return r;
    }
    case Node::Kind::kGrid:
      return measureGrid(n);
    case Node::Kind::kAspectBox:
      return measureAspectBox(n);
    case Node::Kind::kWrapRow:
      return measureWrapRow(n, c);
    case Node::Kind::kBaselineBox:
      return measureBaselineBox(n);
    case Node::Kind::kUnsupported:
      return unsupported();
  }
  return unsupported();
}

}  // namespace layout
