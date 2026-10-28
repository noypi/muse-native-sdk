// T1 hermetic unit test — tile rasterizer (FR-GRAPHICS-001, AC-GRAPHICS-002).
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "engine/graphics/vector/tile_raster.h"

static tile_raster::TileGrid Grid2x2() {
  tile_raster::TileGrid g;
  g.rows = 2;
  g.cols = 2;
  g.tile_width = 1.0;
  g.tile_height = 1.0;
  return g;
}

static tile_raster::PathBatch DiagonalPath() {
  tile_raster::PathBatch p;
  p.segments.push_back({{0.0, 0.0}, {2.0, 2.0}});
  return p;
}

int main() {
  // 1. Golden: 2x2 grid over one diagonal path.
  // Row-major order, tile covered bit = 1, empty tile = 0, so the
  // diagonal (0,0)->(2,2) covers tiles 0 and 3: masks == {1, 0, 0, 1}.
  {
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        DiagonalPath(), Grid2x2(), tile_raster::BackendKind::kCpu);
    assert(r.status == tile_raster::Status::kOk);
    assert(r.masks.size() == 4);
    assert(r.masks[0] == 1u);
    assert(r.masks[1] == 0u);
    assert(r.masks[2] == 0u);
    assert(r.masks[3] == 1u);
    printf("PASS golden-2x2-diagonal\n");
  }
  // 2. Empty path input yields empty output, no error.
  {
    tile_raster::PathBatch empty;
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        empty, Grid2x2(), tile_raster::BackendKind::kCpu);
    assert(r.status == tile_raster::Status::kOk);
    assert(r.masks.empty());
    printf("PASS empty-input-empty-output\n");
  }
  // 3. Unknown backend yields kUnsupportedBackend.
  {
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        DiagonalPath(), Grid2x2(), tile_raster::BackendKind::kUnknown);
    assert(r.status == tile_raster::Status::kUnsupportedBackend);
    printf("PASS unknown-backend-unsupported\n");
  }
  // 4. Boundary: 1x1 grid, path crossing the single tile is covered.
  {
    tile_raster::TileGrid g;
    g.rows = 1;
    g.cols = 1;
    g.tile_width = 2.0;
    g.tile_height = 2.0;
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        DiagonalPath(), g, tile_raster::BackendKind::kCpu);
    assert(r.status == tile_raster::Status::kOk);
    assert(r.masks.size() == 1);
    assert(r.masks[0] == 1u);
    printf("PASS single-tile-covered\n");
  }
  // 5. Boundary: path fully outside the grid covers no tiles, still kOk.
  {
    tile_raster::PathBatch p;
    p.segments.push_back({{10.0, 10.0}, {20.0, 20.0}});
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        p, Grid2x2(), tile_raster::BackendKind::kCpu);
    assert(r.status == tile_raster::Status::kOk);
    assert(r.masks.size() == 4);
    for (uint32_t m : r.masks) assert(m == 0u);
    printf("PASS off-grid-path-empty-masks\n");
  }
  // 6. Security: oversize grid (10000x10000) is rejected with kInvalidArg
  // and empty masks — no allocation blowup.
  {
    tile_raster::TileGrid g;
    g.rows = 10000;
    g.cols = 10000;
    g.tile_width = 1.0;
    g.tile_height = 1.0;
    tile_raster::CoverageResult r = tile_raster::rasterizeTiles(
        DiagonalPath(), g, tile_raster::BackendKind::kCpu);
    assert(r.status == tile_raster::Status::kInvalidArg);
    assert(r.masks.empty());
    printf("PASS oversize-grid-invalid-arg-empty\n");
  }
  // 7. Security: NaN/Inf coords yield a clean empty result (kOk) with no
  // NaN propagation into masks.
  {
    tile_raster::PathBatch p;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    p.segments.push_back({{nan, 0.0}, {2.0, 2.0}});
    tile_raster::CoverageResult r1 = tile_raster::rasterizeTiles(
        p, Grid2x2(), tile_raster::BackendKind::kCpu);
    assert(r1.status == tile_raster::Status::kOk);
    assert(r1.masks.empty());
    tile_raster::PathBatch q;
    q.segments.push_back({{0.0, 0.0}, {inf, inf}});
    tile_raster::CoverageResult r2 = tile_raster::rasterizeTiles(
        q, Grid2x2(), tile_raster::BackendKind::kCpu);
    assert(r2.status == tile_raster::Status::kOk);
    assert(r2.masks.empty());
    tile_raster::TileGrid bad = Grid2x2();
    bad.tile_width = nan;
    tile_raster::CoverageResult r3 = tile_raster::rasterizeTiles(
        DiagonalPath(), bad, tile_raster::BackendKind::kCpu);
    assert(r3.status == tile_raster::Status::kOk);
    assert(r3.masks.empty());
    printf("PASS nan-inf-coords-clean-empty\n");
  }
  printf("ALL T1 TESTS PASSED\n");
  return 0;
}
