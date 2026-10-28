#include "tile_raster.h"

namespace {

// True when the segment passes through the axis-aligned rect with non-zero
// length inside it (Liang-Barsky clip). A touch at a single corner/edge
// point does not count as coverage.
bool SegmentCoversRect(const Segment& s, double xmin, double ymin, double xmax,
                       double ymax) {
  const double dx = s.b.x - s.a.x;
  const double dy = s.b.y - s.a.y;
  double t0 = 0.0;
  double t1 = 1.0;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {s.a.x - xmin, xmax - s.a.x, s.a.y - ymin, ymax - s.a.y};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) return false;  // Parallel and outside.
    } else {
      const double r = q[i] / p[i];
      if (p[i] < 0.0) {
        if (r > t1) return false;
        if (r > t0) t0 = r;
      } else {
        if (r < t0) return false;
        if (r < t1) t1 = r;
      }
    }
  }
  constexpr double kEps = 1e-9;
  return (t1 - t0) > kEps;
}

}  // namespace

CoverageResult rasterizeTiles(const PathBatch& paths, const TileGrid& grid,
                              BackendKind backend) {
  if (backend != BackendKind::kCpu && backend != BackendKind::kVulkan) {
    CoverageResult r;
    r.status = Status::kUnsupportedBackend;
    return r;
  }
  if (paths.segments.empty()) {
    CoverageResult r;
    r.status = Status::kOk;
    return r;
  }
  CoverageResult result;
  result.status = Status::kOk;
  if (grid.rows <= 0 || grid.cols <= 0 || grid.tile_width <= 0.0 ||
      grid.tile_height <= 0.0) {
    return result;  // Degenerate grid: kOk with empty masks.
  }
  result.masks.assign(static_cast<size_t>(grid.rows * grid.cols), 0u);
  for (int row = 0; row < grid.rows; ++row) {
    for (int col = 0; col < grid.cols; ++col) {
      const double xmin = col * grid.tile_width;
      const double ymin = row * grid.tile_height;
      const double xmax = xmin + grid.tile_width;
      const double ymax = ymin + grid.tile_height;
      uint32_t mask = 0u;
      for (const Segment& seg : paths.segments) {
        if (SegmentCoversRect(seg, xmin, ymin, xmax, ymax)) {
          mask = 1u;
          break;
        }
      }
      result.masks[static_cast<size_t>(row * grid.cols + col)] = mask;
    }
  }
  return result;
}
