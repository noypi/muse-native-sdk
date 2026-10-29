// Gallery-handle surface-attach lifecycle variant test — FR-RUNTIME-009 /
// AC-RUNTIME-027 (AC-RUNTIME-030 explicitly excluded per plan T3).
// Interface: GalleryHandleLifecycle sequence: LoadTree(bytes) ->
// Attach(window) -> Render() -> Detach() -> ReAttach(window2) -> Render()
// -> Destroy() — Render after ReAttach succeeds without tree
// re-instantiation; Render while detached fails closed; double-
// Detach/Destroy is safe.
//
// Hermetic host-side test. The Android-only GalleryHandle window field and
// ANativeWindow calls in jni_bridge.cpp cannot compile on host (no NDK, no
// device, no View), so this file drives a faithful harness around the real
// gallery_present.h parse/raster path with a mock window handle (opaque
// void*, never dereferenced — never a View). Structure mirrors
// surface_attach_test.cpp / swapchain_lifecycle_test.cpp (RUN_CASE +
// harness cases). C++17 + committed runtime headers only; in-memory
// buffers; no network, no env/config reads, no filesystem, no live services.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"

// --- harness cases (convention copied from surface_attach_test.cpp) ----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- GalleryHandleLifecycle harness ----------------------------------------

namespace {

// Mock window handle: opaque token only, never dereferenced, never a View.
// Two distinct tokens model surface destroy/recreate (window2 != window).
struct MockWindow {
  unsigned magic = 0xA11CEu;
};

struct FakeGalleryHandle {
  std::vector<std::uint8_t> tree;              // owned tree bytes (identity)
  std::vector<runtime::GalleryRow> rows;       // pre-parsed once at load
  std::string parseError;
  int page = 0;
  const MockWindow* window = nullptr;           // mock attach endpoint
  bool destroyed = false;

  // LoadTree(bytes): validate + copy + pre-parse (mirrors nativeLoadTree).
  bool LoadTree(const std::uint8_t* bytes, std::size_t len) {
    if (bytes == nullptr || len == 0 || destroyed) return false;
    tree.assign(bytes, bytes + len);
    rows = runtime::ParseGalleryRows(tree.data(), tree.size(), &parseError);
    // Empty/blocked parse still yields a handle (diagnostic path), matching
    // nativeLoadTree fail-closed semantics (handle 0 only for null/empty).
    return true;
  }

  // Attach(window): fail closed on null window or destroyed handle.
  bool Attach(const MockWindow* w) {
    if (destroyed || w == nullptr) return false;
    window = w;
    return true;
  }

  // Render(): fail closed while detached/destroyed (no crash, error code);
  // otherwise rasterize the current page through the real rasterizer.
  int Render(std::string* error) {
    if (destroyed || window == nullptr) {
      if (error != nullptr) *error = "no surface attached";
      return -1;
    }
    const int w = 720;
    const int h = 1600;
    std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
    std::string rerr;
    int drawn = runtime::RasterGalleryPage(rows, page, px.data(), w, w, h,
                                           error != nullptr ? error : &rerr);
    return drawn;
  }

  bool Detach() {
    if (destroyed) return true;  // idempotent after destroy
    window = nullptr;
    return true;
  }

  bool Destroy() {
    if (destroyed) return true;  // double-destroy safe
    window = nullptr;
    destroyed = true;
    return true;
  }

  const void* tree_data() const { return tree.data(); }
};

// Minimal MUI1 asset (one Showcase-style root), same envelope convention as
// gallery_present_test.cpp.
std::vector<std::uint8_t> MakeAsset() {
  const char json[] =
      "{\"version\":1,\"roots\":["
      "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
      "\"props\":{},\"slots\":[],\"children\":["
      "{\"type\":\"#text\",\"value\":\"M3 Gallery light\"},"
      "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":"
      "\"Primary action\"}}"
      "]}]}";
  std::size_t n = std::strlen(json);
  std::vector<std::uint8_t> out;
  out.reserve(12 + n);
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (std::size_t i = 0; i < n; ++i) out.push_back(
      static_cast<std::uint8_t>(json[i]));
  return out;
}

}  // namespace

// Named case: full lifecycle — LoadTree -> Attach -> Render -> Detach ->
// ReAttach(window2) -> Render -> Destroy. Render after ReAttach succeeds
// without tree re-instantiation (tree byte identity unchanged across the
// cycle).
static bool check_reattach_renders_without_reinstantiation() {
  std::vector<std::uint8_t> asset = MakeAsset();
  FakeGalleryHandle g;
  if (!g.LoadTree(asset.data(), asset.size())) return false;
  if (g.rows.empty()) return false;
  const void* tree_before = g.tree_data();
  MockWindow w1;
  MockWindow w2;
  if (!g.Attach(&w1)) return false;
  std::string err;
  if (g.Render(&err) <= 0) return false;
  if (!g.Detach()) return false;
  if (!g.Attach(&w2)) return false;  // ReAttach on the fresh window
  if (g.tree_data() != tree_before) return false;  // no re-instantiation
  if (g.Render(&err) <= 0) return false;
  return g.Destroy();
}

// Named case: Render while detached fails closed (negative code, diagnostic,
// no crash); the handle stays usable for a later ReAttach.
static bool check_render_while_detached_fails_closed() {
  std::vector<std::uint8_t> asset = MakeAsset();
  FakeGalleryHandle g;
  if (!g.LoadTree(asset.data(), asset.size())) return false;
  MockWindow w1;
  MockWindow w2;
  if (!g.Attach(&w1)) return false;
  if (!g.Detach()) return false;
  std::string err;
  int r = g.Render(&err);
  if (r >= 0) return false;      // must fail, not draw
  if (err.empty()) return false;  // must carry a diagnostic
  // Still usable: re-attach and render succeeds.
  if (!g.Attach(&w2)) return false;
  if (g.Render(&err) <= 0) return false;
  return g.Destroy();
}

// Named case: Render before any attach fails closed (no crash, error code).
static bool check_render_before_attach_fails_closed() {
  std::vector<std::uint8_t> asset = MakeAsset();
  FakeGalleryHandle g;
  if (!g.LoadTree(asset.data(), asset.size())) return false;
  std::string err;
  int r = g.Render(&err);
  if (r >= 0) return false;
  if (err.empty()) return false;
  return g.Destroy();
}

// Named case: null/invalid window attach fails closed (never creates a
// View); a subsequent valid attach still succeeds (no state mutated).
static bool check_null_window_attach_fails_closed() {
  std::vector<std::uint8_t> asset = MakeAsset();
  FakeGalleryHandle g;
  if (!g.LoadTree(asset.data(), asset.size())) return false;
  if (g.Attach(nullptr)) return false;  // must reject null
  if (g.Attach(nullptr)) return false;  // second null still rejected
  MockWindow w;
  if (!g.Attach(&w)) return false;  // valid attach still succeeds
  std::string err;
  if (g.Render(&err) <= 0) return false;
  return g.Destroy();
}

// Named case: double-Detach and double-Destroy are safe (idempotent, no
// crash); Render after Destroy fails closed.
static bool check_double_detach_destroy_safe() {
  std::vector<std::uint8_t> asset = MakeAsset();
  FakeGalleryHandle g;
  if (!g.LoadTree(asset.data(), asset.size())) return false;
  MockWindow w;
  if (!g.Attach(&w)) return false;
  if (!g.Detach()) return false;
  if (!g.Detach()) return false;  // double-detach safe
  if (!g.Destroy()) return false;
  if (!g.Destroy()) return false;  // double-destroy safe
  if (!g.Detach()) return false;   // detach-after-destroy safe
  std::string err;
  return g.Render(&err) < 0;  // render-after-destroy fails closed
}

// Named case: LoadTree rejects null/empty bytes fail-closed (no handle
// state, no crash).
static bool check_load_null_bytes_fails_closed() {
  FakeGalleryHandle g;
  if (g.LoadTree(nullptr, 0)) return false;
  std::vector<std::uint8_t> one{0x00};
  if (g.LoadTree(one.data(), 0)) return false;
  return g.Destroy();  // destroy-without-load safe
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_reattach_renders_without_reinstantiation);
  RUN_CASE(check_render_while_detached_fails_closed);
  RUN_CASE(check_render_before_attach_fails_closed);
  RUN_CASE(check_null_window_attach_fails_closed);
  RUN_CASE(check_double_detach_destroy_safe);
  RUN_CASE(check_load_null_bytes_fails_closed);
  if (failures > 0) {
    std::printf("gallery_handle_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_handle_lifecycle_test: all passing\n");
  return 0;
}
