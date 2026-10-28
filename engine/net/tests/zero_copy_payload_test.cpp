// Batch 4 contract tests — T8 zero-copy half (FR-NET-010 / AC-NET-019;
// RULE-NET-004; TC-NET-019; FR-CORE-004 AC-CORE-011/013/014 consumed
// envelope + ring shape only).
//
// Hermetic: C++17 + engine/net headers only. No sockets, no env/config
// reads, no filesystem, no live services, no wall-clock sleeps. The
// "shared memory" under test is the process mapping the ring manages
// (fd passing / JSI handle table are core-owned per the plan and are
// explicitly not exercised here).
//
// Binds to the T8 `Interface:` line in plan.md for the zero-copy half:
//   struct muse::net::Envelope { uint32_t schemaId; uint16_t
//     schemaVersion; uint16_t abiVersion; uint32_t payloadLen; };
//   class muse::net::SharedRing { explicit SharedRing(size_t bytes);
//     std::string_view place(const Envelope& e, std::string_view
//       payload); std::string_view consume(uint64_t token); };
//   std::string_view muse::net::ConsumeInPlace(const Envelope& e,
//     std::string_view mapped);
// consume returns a view into the original mapping — zero copies beyond
// the initial map; version mismatch → typed error string, never OOB.
// `none — ring transport mechanics beyond this`.
//
// ASSUMPTIONS:
//   A-TOKEN: `place` stores one record and `consume(n)` retrieves the
//     n-th placed record (0-based insertion index). If the coder's token
//     is instead derived from place's return value, these call sites gain
//     a token translation — expected views do not change.
//   A-ALLOC: the render path performs zero heap allocations. This test
//     enforces it with a test-local counting global operator new/delete
//     armed only around the consume section (post-setup). The counter
//     lives entirely in this file; production code is untouched.
//   A-MISMATCH: "typed error string" for version mismatch is observed as
//     an EMPTY view from ConsumeInPlace/consume (no OOB bytes, no throw).
//     There is no separate error-string accessor in the Interface line;
//     see contract T8.md Conflicts — if the coder adds one, a case is
//     added asserting its value; no existing expectation changes.
//   A-WRAP: the ring capacity is finite; placing more than fits wraps
//     around and the newest records remain consumable in order.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <string_view>

#include "net/zero_copy.h"

namespace {

// --- test-local allocation counter (A-ALLOC) ---
bool g_counting = false;
int g_allocs = 0;

}  // namespace

void* operator new(std::size_t n) {
  if (g_counting) ++g_allocs;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
  if (g_counting) ++g_allocs;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using muse::net::ConsumeInPlace;
using muse::net::Envelope;
using muse::net::SharedRing;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool check_envelope_shape() {
  // AC-CORE-011 (consumed): every payload carries schema id, schema
  // version, ABI version, payload length with the exact widths.
  Envelope e{0xABu, 2u, 3u, 9u};
  CHECK(sizeof(e.schemaId) == 4);
  CHECK(sizeof(e.schemaVersion) == 2);
  CHECK(sizeof(e.abiVersion) == 2);
  CHECK(sizeof(e.payloadLen) == 4);
  CHECK(e.schemaId == 0xABu);
  CHECK(e.schemaVersion == 2u);
  CHECK(e.abiVersion == 3u);
  CHECK(e.payloadLen == 9u);
  return g_failed_checks == 0;
}

bool check_consume_in_place_zero_copy() {
  // AC-NET-019: FlatBuffers/shared-memory payloads are consumed in
  // place — the returned view points INSIDE the original mapping.
  const std::string mapped = "flatbuffers-payload-bytes";
  Envelope e{1, 1, 1, static_cast<uint32_t>(mapped.size())};
  std::string_view v;
  try {
    v = ConsumeInPlace(e, mapped);
  } catch (...) {
    std::printf("    ConsumeInPlace threw\n");
    return false;
  }
  CHECK(!v.empty());
  CHECK(v.data() >= mapped.data());
  CHECK(v.data() + v.size() <= mapped.data() + mapped.size());
  CHECK(v == mapped);
  return g_failed_checks == 0;
}

bool check_consume_in_place_zero_allocs_post_setup() {
  // AC-CORE-014 (consumed, allocation half): zero heap allocations per
  // message after setup on the render path.
  const std::string mapped = "render-path-payload";
  Envelope e{1, 1, 1, static_cast<uint32_t>(mapped.size())};
  // Warm-up (setup, uncounted).
  (void)ConsumeInPlace(e, mapped);
  g_allocs = 0;
  g_counting = true;
  std::string_view v;
  bool threw = false;
  try {
    for (int i = 0; i < 10000; ++i) {
      v = ConsumeInPlace(e, mapped);
      // Touch the view so the loop cannot be optimized away.
      volatile char c = v.empty() ? 0 : v[0];
      (void)c;
    }
  } catch (...) {
    threw = true;
  }
  g_counting = false;
  CHECK(!threw);
  CHECK(g_allocs == 0);
  if (g_allocs != 0)
    std::printf("    render path allocated %d times\n", g_allocs);
  return g_failed_checks == 0;
}

bool check_version_mismatch_never_oob() {
  // AC-CORE-012/014 (consumed): mismatch rejected before any buffer is
  // interpreted — empty view (A-MISMATCH), never OOB, never throw.
  const std::string mapped = "0123456789abcdef";
  Envelope base{1, 1, 1, 8};
  Envelope bad_schema{2, 1, 1, 8};   // wrong schema id
  Envelope bad_schema_v{1, 99, 1, 8};  // wrong schema version
  Envelope bad_abi{1, 1, 99, 8};     // wrong ABI version
  Envelope bad_len{1, 1, 1, 1u << 30};  // length far beyond mapping
  const Envelope cases[] = {bad_schema, bad_schema_v, bad_abi, bad_len};
  for (const Envelope& e : cases) {
    std::string_view v;
    bool threw = false;
    try {
      v = ConsumeInPlace(e, mapped);
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
    CHECK(v.empty());
    if (!v.empty()) {
      CHECK(v.data() >= mapped.data());
      CHECK(v.data() + v.size() <= mapped.data() + mapped.size());
    }
  }
  // Control: the well-formed envelope still consumes.
  std::string_view good;
  try {
    good = ConsumeInPlace(base, mapped);
  } catch (...) {
    return false;
  }
  CHECK(!good.empty());
  return g_failed_checks == 0;
}

bool check_ring_place_consume_order() {
  // AC-CORE-013 (consumed shape): pre-allocated ring, no per-message
  // heap allocation visible to the caller; records come back in order.
  SharedRing ring(4096);
  Envelope e1{1, 1, 1, 5};
  Envelope e2{1, 1, 1, 5};
  std::string_view s1, s2, c0, c1;
  try {
    s1 = ring.place(e1, "hello");
    s2 = ring.place(e2, "world");
    c0 = ring.consume(0);
    c1 = ring.consume(1);
  } catch (...) {
    std::printf("    ring threw\n");
    return false;
  }
  CHECK(c0 == "hello");
  CHECK(c1 == "world");
  (void)s1;
  (void)s2;
  return g_failed_checks == 0;
}

bool check_ring_wrap_around() {
  // Ring wrap-around: overfilling keeps the newest records consumable
  // in order (A-WRAP).
  SharedRing ring(64);  // tiny on purpose
  try {
    for (int i = 0; i < 20; ++i) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "m%02d", i);
      Envelope e{1, 1, 1, 3};
      (void)ring.place(e, std::string_view(buf, 3));
    }
    // Newest record must still be intact and in-mapping.
    std::string_view last = ring.consume(19);
    CHECK(last == "m19");
  } catch (...) {
    std::printf("    ring wrap threw\n");
    return false;
  }
  return g_failed_checks == 0;
}

bool check_ring_consume_after_eviction_is_empty() {
  // Evicted records are gone: after wrap-around, consuming an evicted
  // token returns an empty view (never OOB, never throws) while the
  // newest record stays intact.
  SharedRing ring(64);  // tiny on purpose
  try {
    for (int i = 0; i < 30; ++i) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "m%02d", i);
      Envelope e{1, 1, 1, 3};
      (void)ring.place(e, std::string_view(buf, 3));
    }
    std::string_view evicted;
    bool threw = false;
    try {
      evicted = ring.consume(0);  // long evicted by the 30-record overflow
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
    CHECK(evicted.empty());
    std::string_view last = ring.consume(29);
    CHECK(last == "m29");
  } catch (...) {
    std::printf("    ring eviction threw\n");
    return false;
  }
  return g_failed_checks == 0;
}

bool check_ring_consume_zero_allocs() {
  SharedRing ring(4096);
  Envelope e{1, 1, 1, 4};
  (void)ring.place(e, "data");
  (void)ring.consume(0);  // warm-up
  g_allocs = 0;
  g_counting = true;
  bool threw = false;
  try {
    for (int i = 0; i < 10000; ++i) {
      std::string_view v = ring.consume(0);
      volatile char c = v.empty() ? 0 : v[0];
      (void)c;
    }
  } catch (...) {
    threw = true;
  }
  g_counting = false;
  CHECK(!threw);
  CHECK(g_allocs == 0);
  if (g_allocs != 0)
    std::printf("    ring consume allocated %d times\n", g_allocs);
  return g_failed_checks == 0;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    g_failed_checks = 0;               \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_envelope_shape);
  RUN_CASE(check_consume_in_place_zero_copy);
  RUN_CASE(check_consume_in_place_zero_allocs_post_setup);
  RUN_CASE(check_version_mismatch_never_oob);
  RUN_CASE(check_ring_place_consume_order);
  RUN_CASE(check_ring_wrap_around);
  RUN_CASE(check_ring_consume_after_eviction_is_empty);
  RUN_CASE(check_ring_consume_zero_allocs);
  if (failures > 0) {
    std::printf("zero_copy_payload_test: %d failing\n", failures);
    return 1;
  }
  std::printf("zero_copy_payload_test: all passing\n");
  return 0;
}
