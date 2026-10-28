#ifndef ENGINE_GRAPHICS_VECTOR_TILE_RASTER_H_
#define ENGINE_GRAPHICS_VECTOR_TILE_RASTER_H_

// T1: GPU compute tile-path vector rasterizer core (CPU mirror of the
// compute-shader algorithm). Backend-agnostic dispatch: dispatch enum only,
// no Vulkan calls.

#include <cstdint>
#include <vector>

namespace tile_raster {

enum class BackendKind { kCpu = 0, kVulkan = 1, kUnknown = 999 };

enum class Status { kOk = 0, kUnsupportedBackend = 1, kInvalidArg = 2 };

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Segment {
  Point a;
  Point b;
};

struct PathBatch {
  std::vector<Segment> segments;
};

struct TileGrid {
  int rows = 0;
  int cols = 0;
  double tile_width = 0.0;
  double tile_height = 0.0;
};

struct CoverageResult {
  Status status = Status::kOk;
  // Row-major per-tile coverage bitmasks, size == rows * cols.
  std::vector<uint32_t> masks;
};

// Bin path segments into fixed tiles and compute analytic coverage per tile.
// Returns per-tile coverage bitmask list (covered tile = 1, empty = 0).
// Empty path input yields an empty result with Status::kOk.
// Unknown BackendKind yields Status::kUnsupportedBackend.
// Degenerate grids (rows/cols <= 0, non-finite or non-positive tile sizes,
// or any non-finite segment coordinate) yield Status::kOk with empty masks.
// Oversize grids (rows*cols > 4096 tiles, computed in 64-bit arithmetic)
// yield Status::kInvalidArg with empty masks.
CoverageResult rasterizeTiles(const PathBatch& paths, const TileGrid& grid,
                              BackendKind backend);

}  // namespace tile_raster

#endif  // ENGINE_GRAPHICS_VECTOR_TILE_RASTER_H_
