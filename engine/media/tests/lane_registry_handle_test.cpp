// TC-MEDIA-022/023/024 — lane-first registry-handle lane (FR-MEDIA-014).
// AC-MEDIA-022: nonzero assetId survives DrawCmd copy; distinct sources get
//   distinct stable handles (repeat resolve returns the same handle).
// AC-MEDIA-023: assetId == 0 (unresolvable handle) renders the current solid
//   fill (fail-closed); never blank, never transparent, never throws.
// AC-MEDIA-024: decode happens exactly once per source; post-warmup swaps are
//   memory hits; evictMemory degrades memory -> disk and the disk hit
//   repromotes to memory with no re-decode.
//
// Hermetic: C++17 only, fully in-file doubles (no production registry source
// exists yet under engine/media — see support.md media-lane-registry). No
// network, no env/config reads, no filesystem, no live services, no new deps.
// Follows the engine/media/tests convention (CHECK harness + main returning
// nonzero on failure, "ALL GREEN <name>" sentinel).
//
// Per-item contract (GR-08: this lane has no workflow.local unit-tests/ dir
// convention for media — contract stated here and in the final report):
//   Public interface (in-test double, NOT a production signature claim):
//     `LaneRegistry::resolve(source: string) -> uint32_t` (0 == unresolvable)
//     `DrawCmd{ Payload{ uint32_t assetId } }` (copyable; assetId must survive)
//     `Swapchain::lookup(assetId) -> Pixel` (0 -> current solid fill)
//     `LaneRegistry::{ decodeCount, hitMemory, hitDisk, evictMemory() }`
//   Real adapter: none exists yet (grep: no registry under engine/media) —
//     flagged for the coder lane if a minimal production unit is chosen.
//   Contract suite: the cases below; any future Real must pass them unchanged.
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

// --- in-test doubles (carriage-to-handle only; upload/blit stay
// --- graphics-owned per CONFLICT-MEDIA-003) ---------------------------------
namespace lane_registry_test {

using AssetId = std::uint32_t;

constexpr AssetId kUnresolved = 0;

// Opaque solid-fill sentinel: the "current solid fill" the Swapchain renders
// for assetId == 0. Distinct from blank (0x00000000) and transparent.
struct Pixel {
  std::uint8_t r, g, b, a;
  bool operator==(const Pixel& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  bool operator!=(const Pixel& o) const { return !(*this == o); }
};

constexpr Pixel kSolidFill{0x2A, 0x7D, 0xC4, 0xFF};
constexpr Pixel kBlank{0, 0, 0, 0};
constexpr Pixel kTransparent{0, 0, 0, 0};

// Minimal registry double: source string -> stable nonzero handle on first
// resolve (decodes once), memory-hit on repeat resolves. evictMemory() drops
// the memory index but keeps the disk index; the next resolve is a disk hit
// that repromotes to memory without re-decoding. Empty/hostile sources are
// unresolvable (handle 0) and never throw.
class LaneRegistry {
 public:
  // Incremented exactly once per actual decode (first resolve per source).
  std::size_t decodeCount = 0;
  std::size_t memoryHits = 0;
  std::size_t diskHits = 0;

  // Resolves a source to a stable nonzero handle; 0 == unresolvable.
  // Never throws.
  AssetId resolve(const std::string& source) noexcept {
    try {
      if (source.empty() || source.size() > 4096) return kUnresolved;
      auto mem = memory_.find(source);
      if (mem != memory_.end()) {
        ++memoryHits;
        return mem->second;
      }
      auto disk = disk_.find(source);
      if (disk != disk_.end()) {
        ++diskHits;
        memory_[source] = disk->second;  // repromote
        return disk->second;
      }
      const AssetId h = nextHandle_++;
      memory_[source] = h;
      disk_[source] = h;
      ++decodeCount;
      return h;
    } catch (...) {
      return kUnresolved;
    }
  }

  void evictMemory() noexcept {
    try {
      memory_.clear();
    } catch (...) {
    }
  }

 private:
  std::unordered_map<std::string, AssetId> memory_;
  std::unordered_map<std::string, AssetId> disk_;
  AssetId nextHandle_ = 1;  // 0 is reserved for kUnresolved
};

// DrawCmd carriage double: payload.assetId must survive value copies.
struct DrawCmd {
  struct Payload {
    AssetId assetId = kUnresolved;
  };
  Payload payload;
};

// Swapchain texture-lookup double: nonzero handle -> its pixel; 0 -> the
// current solid fill. Never blank, never transparent, never throws.
class Swapchain {
 public:
  Pixel lookup(AssetId id) const noexcept {
    if (id == kUnresolved) return kSolidFill;
    auto it = pixels_.find(id);
    if (it != pixels_.end()) return it->second;
    return kSolidFill;  // unknown handle fails closed too
  }
  void admit(AssetId id, Pixel p) { pixels_[id] = p; }

 private:
  std::unordered_map<AssetId, Pixel> pixels_;
};

}  // namespace lane_registry_test

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

using lane_registry_test::AssetId;
using lane_registry_test::DrawCmd;
using lane_registry_test::kBlank;
using lane_registry_test::kSolidFill;
using lane_registry_test::kTransparent;
using lane_registry_test::kUnresolved;
using lane_registry_test::LaneRegistry;
using lane_registry_test::Pixel;
using lane_registry_test::Swapchain;

// --- TC-MEDIA-022 / AC-MEDIA-022 --------------------------------------------

// Distinct sources resolve to distinct nonzero stable handles.
static void check_distinct_sources_distinct_stable_handles() {
  LaneRegistry r;
  const AssetId a = r.resolve("https://img.test/bird1.png");
  const AssetId b = r.resolve("https://img.test/bird2.png");
  CHECK("handles are nonzero", a != kUnresolved && b != kUnresolved);
  CHECK("distinct sources get distinct handles", a != b);
  CHECK("repeat resolve of A is stable", r.resolve("https://img.test/bird1.png") == a);
  CHECK("repeat resolve of B is stable", r.resolve("https://img.test/bird2.png") == b);
}

// A nonzero assetId survives DrawCmd copy intact end-to-end
// (source -> handle -> DrawCmd copy -> Swapchain lookup).
static void check_assetid_survives_drawcmd_copy() {
  LaneRegistry r;
  Swapchain swap;
  const AssetId h = r.resolve("https://img.test/sprite.png");
  CHECK("source resolves to nonzero handle", h != kUnresolved);
  const Pixel want{0xDE, 0xAD, 0xBE, 0xFF};
  swap.admit(h, want);
  DrawCmd cmd;
  cmd.payload.assetId = h;
  const DrawCmd copy = cmd;  // value copy, as issued down the lane
  CHECK("assetId survives DrawCmd copy", copy.payload.assetId == h);
  CHECK("assetId survives second copy", DrawCmd(copy).payload.assetId == h);
  bool threw = false;
  Pixel got{0, 0, 0, 0};
  try {
    got = swap.lookup(copy.payload.assetId);
  } catch (...) {
    threw = true;
  }
  CHECK("swapchain lookup never throws", !threw);
  CHECK("end-to-end pixel matches admitted pixel", got == want);
}

// --- TC-MEDIA-023 / AC-MEDIA-023 --------------------------------------------

// assetId == 0 resolves to the current solid fill — never blank, never
// transparent, never throws (including via a DrawCmd copy carrying 0).
static void check_zero_handle_fail_closed_solid_fill() {
  Swapchain swap;
  bool threw = false;
  Pixel got{0, 0, 0, 0};
  try {
    got = swap.lookup(kUnresolved);
  } catch (...) {
    threw = true;
  }
  CHECK("zero-handle lookup never throws", !threw);
  CHECK("zero handle renders current solid fill", got == kSolidFill);
  CHECK("zero handle never renders blank", got != kBlank);
  CHECK("zero handle never renders transparent",
        !(got.a == kTransparent.a && got.r == 0 && got.g == 0 && got.b == 0));
  // Same guarantee through a copied DrawCmd carrying assetId == 0.
  DrawCmd cmd;
  cmd.payload.assetId = kUnresolved;
  const DrawCmd copy = cmd;
  Pixel via_cmd{0, 0, 0, 0};
  try {
    via_cmd = swap.lookup(copy.payload.assetId);
  } catch (...) {
    threw = true;
  }
  CHECK("zero handle via DrawCmd copy never throws", !threw);
  CHECK("zero handle via DrawCmd copy is solid fill", via_cmd == kSolidFill);
}

// Unresolvable sources (empty / oversized-hostile) yield handle 0 and still
// fail closed to solid fill without throwing.
static void check_unresolvable_source_fails_closed() {
  LaneRegistry r;
  Swapchain swap;
  bool threw = false;
  AssetId empty_h = kUnresolved + 1;
  AssetId evil_h = kUnresolved + 1;
  try {
    empty_h = r.resolve(std::string());
    evil_h = r.resolve(std::string("https://img.test/") + std::string(8192, 'A'));
  } catch (...) {
    threw = true;
  }
  CHECK("unresolvable resolve never throws", !threw);
  CHECK("empty source resolves to 0", empty_h == kUnresolved);
  CHECK("oversize hostile source resolves to 0", evil_h == kUnresolved);
  CHECK("empty source fails closed to solid fill", swap.lookup(empty_h) == kSolidFill);
  CHECK("hostile source fails closed to solid fill", swap.lookup(evil_h) == kSolidFill);
}

// --- TC-MEDIA-024 / AC-MEDIA-024 --------------------------------------------

// Decode happens exactly once per source: N post-warmup swaps are memory
// hits and the decode counter stays at 1.
static void check_decode_once_memory_hits_post_warmup() {
  LaneRegistry r;
  const std::string src = "https://img.test/bird.png";
  const AssetId first = r.resolve(src);
  CHECK("first resolve decodes once", r.decodeCount == 1u);
  for (int i = 0; i < 8; ++i) {
    const AssetId h = r.resolve(src);
    CHECK("post-warmup swap resolves same handle", h == first);
  }
  CHECK("decode counter still 1 after N swaps", r.decodeCount == 1u);
  CHECK("post-warmup swaps were memory hits", r.memoryHits == 8u && r.diskHits == 0u);
}

// Eviction degrades memory -> disk (repromote, no re-decode), proven across
// evict -> disk -> memory: disk hit re-resolves the same handle without
// re-decoding, and the following resolve is a memory hit again.
static void check_evict_memory_disk_repromote_no_redecode() {
  LaneRegistry r;
  const std::string src = "https://img.test/cloud.png";
  const AssetId first = r.resolve(src);
  CHECK("warmup decodes once", r.decodeCount == 1u);
  r.evictMemory();
  const AssetId via_disk = r.resolve(src);
  CHECK("disk hit re-resolves same handle", via_disk == first);
  CHECK("disk hit counted", r.diskHits == 1u);
  CHECK("no re-decode on disk hit", r.decodeCount == 1u);
  const AssetId repromoted = r.resolve(src);
  CHECK("repromoted resolve is same handle", repromoted == first);
  CHECK("repromoted resolve is a memory hit", r.memoryHits == 1u);
  CHECK("no re-decode after repromote", r.decodeCount == 1u);
}

int main() {
  check_distinct_sources_distinct_stable_handles();
  check_assetid_survives_drawcmd_copy();
  check_zero_handle_fail_closed_solid_fill();
  check_unresolvable_source_fails_closed();
  check_decode_once_memory_hits_post_warmup();
  check_evict_memory_disk_repromote_no_redecode();
  if (s_failures == 0) {
    std::printf("ALL GREEN lane_registry\n");
    return 0;
  }
  std::printf("%d FAILURES lane_registry\n", s_failures);
  return 1;
}
