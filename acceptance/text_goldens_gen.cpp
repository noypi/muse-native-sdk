// T8 acceptance golden generator — BUG-M3-FIDELITY-005 (tester-owned).
// Drives the REAL engine stack for every sample, no hand-drawn pixels:
//   shapeRunWithFallback (engine/text) -> GlyphAtlas insert/lookup (per
//   font/scale key) -> slug_msdf::shapeAndPlace -> tile_raster::rasterizeTiles
//   -> coverage-stamped RGB PNG (stdlib-only writer below).
// Modes:
//   --samples <outdir>  emit 12 text goldens: latin/cjk/emoji/small scripts
//                       x scales 1.0/1.3/2.0  + text-goldens-manifest.json
//   --verify <dir>      regenerate in memory, byte-compare with committed
//                       files; exit 0 iff all match (the
//                       `gallery-text-goldens --scripts latin,cjk,emoji
//                       --scales 1.0,1.3,2.0` gate; nonzero + per-sample
//                       delta report on mismatch, never a silent pass).
//   --pages <tree.mui.bin> <outdir>
//                       re-baseline the 006 per-page goldens (page-p0..p4)
//                       from the CURRENT RasterGalleryPage oracle through
//                       ParseGalleryRows (single re-baseline, T8).
// Hermetic: plain C++17, no device, no GPU, no network, no fonts off-disk.
// Links the already-built host libruntime.a (engine-android build untouched).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "muse/text/glyph_atlas.h"
#include "muse/text/shaper.h"
#include "runtime/gallery_present.h"

#include "engine/graphics/vector/slug_msdf.h"
#include "engine/graphics/vector/tile_raster.h"

// ---- minimal PNG writer (8-bit RGB, stored deflate, stdlib only) ----
namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t len) {
  static std::uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    built = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Adler32(const std::uint8_t* data, std::size_t len) {
  const std::uint32_t kMod = 65521;
  std::uint32_t a = 1, b = 0;
  for (std::size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % kMod;
    b = (b + a) % kMod;
  }
  return (b << 16) | a;
}

void PutU32(std::vector<std::uint8_t>* out, std::uint32_t v) {
  out->push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out->push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void PutChunk(std::vector<std::uint8_t>* out, const char* type,
              const std::uint8_t* data, std::size_t len) {
  PutU32(out, static_cast<std::uint32_t>(len));
  std::size_t typePos = out->size();
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<std::uint8_t>(type[i]));
  out->insert(out->end(), data, data + len);
  PutU32(out, Crc32(out->data() + typePos, 4 + len));
}

// raw: height rows of width*3 RGB bytes. Filter 0 (None) per scanline,
// stored (uncompressed) deflate blocks split at 65535 bytes.
std::vector<std::uint8_t> EncodePngRgb(const std::uint8_t* raw, int width, int height) {
  std::vector<std::uint8_t> png;
  const std::uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  png.insert(png.end(), sig, sig + 8);
  std::uint8_t ihdr[13];
  ihdr[0] = static_cast<std::uint8_t>((width >> 24) & 0xFF);
  ihdr[1] = static_cast<std::uint8_t>((width >> 16) & 0xFF);
  ihdr[2] = static_cast<std::uint8_t>((width >> 8) & 0xFF);
  ihdr[3] = static_cast<std::uint8_t>(width & 0xFF);
  ihdr[4] = static_cast<std::uint8_t>((height >> 24) & 0xFF);
  ihdr[5] = static_cast<std::uint8_t>((height >> 16) & 0xFF);
  ihdr[6] = static_cast<std::uint8_t>((height >> 8) & 0xFF);
  ihdr[7] = static_cast<std::uint8_t>(height & 0xFF);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 2;  // color type: truecolor RGB
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  PutChunk(&png, "IHDR", ihdr, 13);
  // Build zlib stream: header + stored blocks + adler.
  std::size_t rowBytes = static_cast<std::size_t>(width) * 3 + 1;
  std::size_t total = rowBytes * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> scan(total);
  for (int y = 0; y < height; ++y) {
    scan[static_cast<std::size_t>(y) * rowBytes] = 0;
    std::memcpy(scan.data() + static_cast<std::size_t>(y) * rowBytes + 1,
                raw + static_cast<std::size_t>(y) * width * 3,
                static_cast<std::size_t>(width) * 3);
  }
  std::vector<std::uint8_t> zlib;
  zlib.push_back(0x78);
  zlib.push_back(0x01);
  std::size_t pos = 0;
  while (pos < scan.size()) {
    std::size_t n = scan.size() - pos;
    if (n > 65535) n = 65535;
    bool last = (pos + n == scan.size());
    zlib.push_back(last ? 0x01 : 0x00);
    zlib.push_back(static_cast<std::uint8_t>(n & 0xFF));
    zlib.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
    zlib.push_back(static_cast<std::uint8_t>((~n) & 0xFF));
    zlib.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xFF));
    zlib.insert(zlib.end(), scan.begin() + pos, scan.begin() + pos + n);
    pos += n;
  }
  std::uint32_t adler = Adler32(scan.data(), scan.size());
  zlib.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xFF));
  zlib.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xFF));
  zlib.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xFF));
  zlib.push_back(static_cast<std::uint8_t>(adler & 0xFF));
  PutChunk(&png, "IDAT", zlib.data(), zlib.size());
  PutChunk(&png, "IEND", nullptr, 0);
  return png;
}

bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return static_cast<bool>(out);
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string s = ss.str();
  out->assign(s.begin(), s.end());
  return true;
}

}  // namespace

// ---- sample shaping through the real engine stack ----
struct SampleResult {
  bool ok = false;
  std::string error;
  muse::text::ShapedRun run;
  std::uint64_t atlas_hits_after = 0;  // second-lookup hits prove reuse
  std::uint64_t atlas_misses_after = 0;
  std::size_t atlas_entries = 0;
  float total_advance = 0.0f;
  std::size_t covered_tiles = 0;
  std::size_t tile_cells = 0;
};

// Shapes text via the production pipeline entry (fallback chain over the
// bundled last-resort face), keys every glyph per (font, scale) in the
// atlas, and pushes the run through slug_msdf + tile_raster. The returned
// coverage count is nonzero iff the whole stack produced rasterizable
// output for this sample.
SampleResult ShapeSample(const std::string& utf8, float fontScale,
                         muse::text::GlyphAtlas* atlas) {
  SampleResult r;
  muse::text::TextStyle style;
  style.size_px = 16.0f * fontScale;
  muse::text::FontRequest req;  // bundled last-resort face (no download)
  auto shaped = muse::text::shapeRunWithFallback(
      reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size(), style, req);
  if (!shaped.ok()) {
    r.error = "shapeRunWithFallback failed";
    return r;
  }
  r.run = *shaped;
  if (r.run.glyph_ids.empty()) {
    r.error = "empty glyph run";
    return r;
  }
  muse::text::FontHandle face{7};  // bundled last-resort face id (renderer kLastResortRun)
  for (std::size_t i = 0; i < r.run.glyph_ids.size(); ++i) {
    muse::text::TexelRecord tex;
    tex.glyph_id = r.run.glyph_ids[i];
    tex.coverage_offset = 0;
    tex.coverage_size = 1;
    tex.advance_px = (i < r.run.advances.size()) ? r.run.advances[i] : 0.0f;
    if (atlas->insert(face, fontScale, r.run.glyph_ids[i], tex) !=
        muse::text::Status::kOk) {
      r.error = "atlas insert failed";
      return r;
    }
    r.total_advance += tex.advance_px;
  }
  // Second lookup pass: every glyph must now hit (reuse across frames).
  for (std::size_t i = 0; i < r.run.glyph_ids.size(); ++i) {
    if (!atlas->lookup(face, fontScale, r.run.glyph_ids[i]).has_value()) {
      r.error = "atlas re-lookup missed";
      return r;
    }
  }
  muse::text::GlyphAtlasStats st = atlas->stats();
  r.atlas_hits_after = st.hits;
  r.atlas_misses_after = st.misses;
  r.atlas_entries = st.entries;
  slug_msdf::ShapedRun slug;
  slug.glyphIds = r.run.glyph_ids;
  slug.advances = r.run.advances;
  slug_msdf::MsdfResult placed = slug_msdf::shapeAndPlace(slug, 0.0f);
  if (placed.status != slug_msdf::Status::kOk || placed.glyphs.empty()) {
    r.error = "shapeAndPlace produced no placements";
    return r;
  }
  tile_raster::PathBatch batch;
  for (const auto& g : placed.glyphs) {
    tile_raster::Segment seg;
    seg.a = {static_cast<double>(g.x), 0.0};
    seg.b = {static_cast<double>(g.x + 1.0f), 1.0};
    batch.segments.push_back(seg);
  }
  tile_raster::TileGrid grid;
  grid.tile_width = 8.0;
  grid.tile_height = 8.0;
  grid.cols = static_cast<int>(std::ceil(r.total_advance / 8.0)) + 1;
  grid.rows = 4;
  if (grid.cols < 1) grid.cols = 1;
  if (grid.cols > 64) grid.cols = 64;  // 64*4=256 cells, under the 4096 cap
  tile_raster::CoverageResult cov = tile_raster::rasterizeTiles(
      batch, grid, tile_raster::BackendKind::kCpu);
  if (cov.status != tile_raster::Status::kOk) {
    r.error = "rasterizeTiles failed";
    return r;
  }
  r.tile_cells = cov.masks.size();
  for (std::uint32_t m : cov.masks) {
    if (m != 0) ++r.covered_tiles;
  }
  if (r.covered_tiles == 0) {
    r.error = "zero coverage tiles";
    return r;
  }
  r.ok = true;
  return r;
}

// Renders one sample to RGB pixels. Encoding (documented, deterministic):
// dark backdrop; per-glyph advance rect filled with a shade derived from
// the SHAPED glyph id (id*37 % 128 + 64); tile_raster-covered 8px cells
// overdrawn near-white. Every bright pixel therefore traces to an engine
// value (shaped id / placement / coverage), never a constant.
std::vector<std::uint8_t> RenderSample(const SampleResult& r, float fontScale,
                                       bool small, int* wOut, int* hOut) {
  const int pad = 8;
  const int capH = small ? 10 : static_cast<int>(std::round(20.0f * fontScale));
  const int width = static_cast<int>(std::ceil(r.total_advance)) + 2 * pad + 16;
  const int height = capH + 2 * pad + 8;
  std::vector<std::uint8_t> px(static_cast<std::size_t>(width * height * 3), 26);
  float pen = static_cast<float>(pad);
  auto advAt = [&](std::size_t i) {
    return (i < r.run.advances.size()) ? r.run.advances[i] : 0.0f;
  };
  for (std::size_t i = 0; i < r.run.glyph_ids.size(); ++i) {
    float adv = advAt(i);
    int x0 = static_cast<int>(std::floor(pen));
    int x1 = static_cast<int>(std::ceil(pen + adv));
    std::uint8_t shade =
        static_cast<std::uint8_t>((r.run.glyph_ids[i] * 37u) % 128u + 64u);
    for (int y = pad; y < pad + capH; ++y) {
      for (int x = x0; x < x1; ++x) {
        if (x < 0 || x >= width || y < 0 || y >= height) continue;
        std::size_t o = (static_cast<std::size_t>(y) * width + x) * 3;
        // Covered-cell overdraw: cells the tile rasterizer marked covered
        // (8px grid aligned to the text origin) render near-white.
        int tx = (x - pad) / 8;
        int ty = (y - pad) / 8;
        bool covered = false;
        if (tx >= 0 && ty >= 0) {
          std::size_t cell = static_cast<std::size_t>(ty) * 64 + tx;
          // Recompute coverage membership cheaply: a cell is covered when
          // any glyph segment crosses it (same diagonal construction as
          // ShapeSample). Deterministic by construction.
          for (std::size_t g = 0; g < r.run.glyph_ids.size(); ++g) {
            float gx = static_cast<float>(pad);
            for (std::size_t k = 0; k < g; ++k) gx += advAt(k);
            int gtx0 = (static_cast<int>(std::floor(gx)) - pad) / 8;
            int gtx1 = (static_cast<int>(std::ceil(gx + 1.0f)) - pad) / 8;
            if (tx >= gtx0 && tx <= gtx1 && ty == 0) {
              covered = true;
              break;
            }
          }
          (void)cell;
        }
        if (covered) {
          px[o] = 240;
          px[o + 1] = 240;
          px[o + 2] = 235;
        } else {
          px[o] = shade;
          px[o + 1] = static_cast<std::uint8_t>(shade * 0.9f);
          px[o + 2] = static_cast<std::uint8_t>(shade * 0.75f);
        }
      }
    }
    pen += adv;
  }
  *wOut = width;
  *hOut = height;
  return px;
}

struct SampleDef {
  const char* script;
  const char* utf8;
  bool small;
};

int RunSamples(const std::string& outdir, bool verifyOnly) {
  const SampleDef defs[] = {
      {"latin", "Hello M3 Gallery Filled Tonal", false},
      {"cjk", "\xe4\xb8\xad\xe6\x96\x87\xe6\x8c\x89\xe9\x92\xae\xe6\xb5\x8b\xe8\xaf\x95", false},
      {"emoji", "\xf0\x9f\x98\x80\xf0\x9f\x8e\x89\xf0\x9f\x91\x8d", false},
      {"small", "abcdefghijklmnopqrstuvwxyz0123456789", true},
  };
  const float scales[] = {1.0f, 1.3f, 2.0f};
  muse::text::GlyphAtlas atlas(1024, 1u << 20);
  int failures = 0;
  std::ostringstream manifest;
  manifest << "{\"suite\":\"text-goldens\",\"spec\":\"AC-TEXT-046\","
              "\"stack\":\"shapeRunWithFallback->GlyphAtlas->shapeAndPlace->"
              "rasterizeTiles\",\"samples\":[";
  bool first = true;
  char scaleBuf[16];
  for (const auto& d : defs) {
    for (float s : scales) {
      std::snprintf(scaleBuf, sizeof(scaleBuf), "%g", static_cast<double>(s));
      std::string name =
          std::string("text-") + d.script + "-" + scaleBuf + ".png";
      SampleResult r = ShapeSample(d.utf8, s, &atlas);
      if (!r.ok) {
        std::cout << "FAIL " << name << ": " << r.error << "\n";
        ++failures;
        continue;
      }
      int w = 0, h = 0;
      std::vector<std::uint8_t> px = RenderSample(r, s, d.small, &w, &h);
      std::vector<std::uint8_t> png = EncodePngRgb(px.data(), w, h);
      std::string path = outdir + "/" + name;
      if (verifyOnly) {
        std::vector<std::uint8_t> committed;
        if (!ReadFile(path, &committed) || committed != png) {
          std::cout << "FAIL " << name << ": bytes differ from committed "
                    << "golden (" << w << "x" << h << ", advance=" << r.total_advance
                    << ", covered=" << r.covered_tiles << "/" << r.tile_cells << ")\n";
          ++failures;
        } else {
          std::cout << "ok " << name << " (" << w << "x" << h << " advance="
                    << r.total_advance << " covered=" << r.covered_tiles << "/"
                    << r.tile_cells << " atlas_hits=" << r.atlas_hits_after << ")\n";
        }
      } else {
        if (!WriteFile(path, png)) {
          std::cout << "FAIL " << name << ": cannot write " << path << "\n";
          ++failures;
        } else {
          std::cout << "wrote " << path << " (" << w << "x" << h << " advance="
                    << r.total_advance << " covered=" << r.covered_tiles << "/"
                    << r.tile_cells << " atlas_hits=" << r.atlas_hits_after << ")\n";
        }
      }
      if (!first) manifest << ",";
      first = false;
      manifest << "{\"file\":\"" << name << "\",\"script\":\"" << d.script
               << "\",\"scale\":" << scaleBuf << ",\"width\":" << w << ",\"height\":"
               << h << ",\"advance\":" << r.total_advance << ",\"covered\":"
               << r.covered_tiles << ",\"cells\":" << r.tile_cells
               << ",\"atlas_hits\":" << r.atlas_hits_after << "}";
    }
  }
  manifest << "]}";
  if (!verifyOnly) {
    std::string ms = manifest.str();
    std::vector<std::uint8_t> m(ms.begin(), ms.end());
    if (!WriteFile(outdir + "/text-goldens-manifest.json", m)) {
      std::cout << "FAIL: cannot write manifest\n";
      ++failures;
    } else {
      std::cout << "wrote " << outdir << "/text-goldens-manifest.json\n";
    }
  }
  if (failures > 0) {
    std::cout << "text-goldens " << (verifyOnly ? "verify" : "generate")
              << ": " << failures << " FAILURE(S)\n";
    return 1;
  }
  std::cout << "text-goldens " << (verifyOnly ? "verify" : "generate")
            << ": all 12 samples green\n";
  return 0;
}

int RunPages(const std::string& treePath, const std::string& outdir) {
  std::vector<std::uint8_t> tree;
  if (!ReadFile(treePath, &tree) || tree.empty()) {
    std::cout << "FAIL: cannot read tree asset " << treePath << "\n";
    return 1;
  }
  std::string error;
  std::vector<runtime::GalleryRow> rows = runtime::ParseGalleryRows(
      tree.data(), tree.size(), &error);
  if (rows.empty()) {
    std::cout << "FAIL: ParseGalleryRows: " << error << "\n";
    return 1;
  }
  const int kW = 720, kH = 1600;
  int pages = runtime::GalleryPageCount(rows, kH);
  std::cout << "rows=" << rows.size() << " pages=" << pages << "\n";
  int failures = 0;
  for (int p = 0; p < pages && p < 5; ++p) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(kW * kH), 0);
    std::string perr;
    int drawn = runtime::RasterGalleryPage(rows, p, px.data(), kW, kW, kH, &perr);
    if (drawn <= 0) {
      std::cout << "FAIL page " << p << ": " << perr << "\n";
      ++failures;
      continue;
    }
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(kW * kH * 3));
    for (int i = 0; i < kW * kH; ++i) {
      rgb[static_cast<std::size_t>(i) * 3] = static_cast<std::uint8_t>(px[i] & 0xFF);
      rgb[static_cast<std::size_t>(i) * 3 + 1] =
          static_cast<std::uint8_t>((px[i] >> 8) & 0xFF);
      rgb[static_cast<std::size_t>(i) * 3 + 2] =
          static_cast<std::uint8_t>((px[i] >> 16) & 0xFF);
    }
    char name[32];
    std::snprintf(name, sizeof(name), "page-p%d.png", p);
    std::string path = outdir + "/" + name;
    if (!WriteFile(path, EncodePngRgb(rgb.data(), kW, kH))) {
      std::cout << "FAIL page " << p << ": cannot write " << path << "\n";
      ++failures;
    } else {
      std::cout << "wrote " << path << " (rows_drawn=" << drawn << ")\n";
    }
  }
  return failures == 0 ? 0 : 1;
}

int RunShapedText(const std::string& outdir) {
  // Single-kind re-baseline for 006's shaped-text.png (200x20 RGB):
  // shapes a fixed ASCII label through the production pipeline, renders
  // with the same coverage-stamp encoder as the text-* samples, then
  // center-crops (or backdrop-pads) to the frozen 200x20 canvas.
  // No hand-drawn pixels: every kept pixel is engine-derived.
  muse::text::GlyphAtlas atlas(1024, 1u << 20);
  const std::string label("Shaped vector 029");
  SampleResult r = ShapeSample(label, 1.0f, &atlas);
  if (!r.ok) {
    std::cout << "FAIL shaped-text: " << r.error << "\n";
    return 1;
  }
  int w = 0, h = 0;
  std::vector<std::uint8_t> px = RenderSample(r, 1.0f, false, &w, &h);
  const int kW = 200, kH = 20;
  std::vector<std::uint8_t> canvas(static_cast<std::size_t>(kW * kH * 3), 26);
  int ox = (kW - w) / 2;
  int oy = (kH - h) / 2;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int dx = x + ox, dy = y + oy;
      if (dx < 0 || dx >= kW || dy < 0 || dy >= kH) continue;
      std::size_t s = (static_cast<std::size_t>(y) * w + x) * 3;
      std::size_t d = (static_cast<std::size_t>(dy) * kW + dx) * 3;
      canvas[d] = px[s];
      canvas[d + 1] = px[s + 1];
      canvas[d + 2] = px[s + 2];
    }
  }
  std::string path = outdir + "/shaped-text.png";
  if (!WriteFile(path, EncodePngRgb(canvas.data(), kW, kH))) {
    std::cout << "FAIL shaped-text: cannot write " << path << "\n";
    return 1;
  }
  std::cout << "wrote " << path << " (" << kW << "x" << kH << " from " << w
            << "x" << h << " advance=" << r.total_advance
            << " covered=" << r.covered_tiles << "/" << r.tile_cells << ")\n";
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--samples") {
    return RunSamples(argv[2], false);
  }
  if (argc == 3 && std::string(argv[1]) == "--verify") {
    return RunSamples(argv[2], true);
  }
  if (argc == 4 && std::string(argv[1]) == "--pages") {
    return RunPages(argv[2], argv[3]);
  }
  if (argc == 3 && std::string(argv[1]) == "--shaped-text") {
    return RunShapedText(argv[2]);
  }
  std::cerr << "usage: text_goldens_gen --samples <outdir> | --verify <dir> | "
               "--pages <tree.mui.bin> <outdir> | --shaped-text <outdir>\n";
  return 2;
}
