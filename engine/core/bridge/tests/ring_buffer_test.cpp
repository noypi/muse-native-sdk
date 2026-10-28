// Batch 2 contract tests — T3 pre-allocated ring, zero per-message heap
// allocation (FR-CORE-004 / AC-CORE-013; TC-CORE-013).
//
// Hermetic: C++17 + engine/core/bridge headers only. No sockets, no
// env/config reads, no filesystem, no live services, no wall-clock.
// Payloads are inline byte arrays; the ring is process memory with the
// same no-per-message-allocation contract as the ASHaredMemory binding
// (plan A3). Allocation behavior is observed with an instrumented
// global operator new counter scoped to the measured section only.
//
// Binds to the T3 `Interface:` line in plan.md:
//   muse::bridge::SharedRing::SharedRing(size_t capacityBytes);
//   std::string_view place(const Envelope&, std::string_view payload);
//   std::string_view consume(uint64_t token) noexcept;
//   allocation-failure place returns empty view, never throws;
//   consume of evicted/never-placed token returns empty view.
//
// ASSUMPTIONS:
//   A1: new module mirroring engine/net SharedRing shape (plan A1).
//   A2: gradlew names stale, CTest used (plan A2).
//   A3: ASHaredMemory fd passing is not hermetically testable; the ring
//     is a pre-allocated fixed-capacity byte ring, capacity counts
//     payload bytes, tokens are 0-based insertion indices, overfill
//     evicts oldest; the fd mapping is a thin shim over it (plan A3).
//   A-ALLOC: zero per-message heap allocation is observed via the
//     instrumented operator new counter below: zero counted operator
//     new calls across place/consume after setup. Setup itself (ring
//     construction, which pre-allocates) may allocate.
//   A-TOKEN: tokens are 0-based insertion indices in place order; the
//     Interface line names the token type (uint64_t) but not its
//     sequence, so tests place N messages and consume tokens 0..N-1.
//   A-EVICT: on overfill the oldest tokens become empty while the
//     newest remain consumable; the Interface line does not pin which
//     end evicts, so the wrap test pins oldest-evicts as the most
//     defensible order (mirrors muse::net::SharedRing).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string_view>

#include "core/bridge/envelope.h"
#include "core/bridge/ring_buffer.h"

// --- Allocation instrumentation (A-ALLOC) ---
// Counts heap allocations via global operator new while g_counting is
// set. The measured sections use only stack storage + string_view, so
// any counted allocation is a per-message heap allocation by the ring.
static long g_alloc_count = 0;
static bool g_counting = false;

void* operator new(std::size_t n) {
  if (g_counting) ++g_alloc_count;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
  if (g_counting) ++g_alloc_count;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using muse::bridge::Envelope;
using muse::bridge::SharedRing;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool check_place_consume_roundtrip() {
  // Happy path: placed payload is consumable by token with identical
  // content.
  SharedRing ring(1024);
  Envelope e{1u, 1u, 1u, 5u};
  const char payload[] = {'h', 'e', 'l', 'l', 'o'};
  std::string_view placed;
  bool threw = false;
  try {
    placed = ring.place(e, std::string_view(payload, sizeof(payload)));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!placed.empty());
  CHECK(placed.size() == 5u);
  std::string_view got;
  try {
    got = ring.consume(0);
  } catch (...) {
    std::printf("    consume threw\n");
    return false;
  }
  CHECK(!got.empty());
  CHECK(got.size() == 5u);
  CHECK(got == std::string_view(payload, sizeof(payload)));
  return g_failed_checks == 0;
}

bool check_consume_never_placed_is_empty() {
  // Consume of a never-placed token returns an empty view, never throws.
  SharedRing ring(1024);
  std::string_view got;
  bool threw = false;
  try {
    got = ring.consume(0);
    CHECK(got.empty());
    got = ring.consume(999999u);
    CHECK(got.empty());
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  return g_failed_checks == 0;
}

bool check_tokens_are_insertion_indices() {
  // A-TOKEN: tokens are 0-based insertion indices; three places are
  // consumable as tokens 0, 1, 2 in order.
  SharedRing ring(4096);
  const char a[] = {'a'};
  const char b[] = {'b', 'b'};
  const char c[] = {'c', 'c', 'c'};
  Envelope e{1u, 1u, 1u, 0u};
  bool threw = false;
  try {
    e.payloadLen = 1u;
    (void)ring.place(e, std::string_view(a, 1));
    e.payloadLen = 2u;
    (void)ring.place(e, std::string_view(b, 2));
    e.payloadLen = 3u;
    (void)ring.place(e, std::string_view(c, 3));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(ring.consume(0) == std::string_view(a, 1));
  CHECK(ring.consume(1) == std::string_view(b, 2));
  CHECK(ring.consume(2) == std::string_view(c, 3));
  return g_failed_checks == 0;
}

bool check_wrap_evicts_oldest() {
  // A-EVICT: overfill evicts oldest while newest remain consumable.
  // Ring holds ~4 payload bytes; placing 3 x 2-byte messages evicts
  // token 0 but keeps tokens 1..2.
  SharedRing ring(4);
  const char m0[] = {'a', 'a'};
  const char m1[] = {'b', 'b'};
  const char m2[] = {'c', 'c'};
  Envelope e{1u, 1u, 1u, 2u};
  bool threw = false;
  try {
    (void)ring.place(e, std::string_view(m0, 2));
    (void)ring.place(e, std::string_view(m1, 2));
    (void)ring.place(e, std::string_view(m2, 2));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  std::string_view evicted;
  std::string_view newest;
  try {
    evicted = ring.consume(0);
    newest = ring.consume(2);
  } catch (...) {
    std::printf("    consume threw\n");
    return false;
  }
  CHECK(evicted.empty());
  CHECK(!newest.empty());
  CHECK(newest == std::string_view(m2, 2));
  return g_failed_checks == 0;
}

bool check_capacity_counts_payload_bytes() {
  // Capacity counts payload bytes: a single payload larger than the
  // ring cannot be retained — place fails with an empty view.
  SharedRing ring(8);
  static const char big[64] = {0};
  Envelope e{1u, 1u, 1u, 64u};
  std::string_view placed{"x", 1};  // sentinel non-empty
  bool threw = false;
  try {
    placed = ring.place(e, std::string_view(big, sizeof(big)));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(placed.empty());
  return g_failed_checks == 0;
}

bool check_place_allocation_failure_empty_never_throws() {
  // Allocation-failure place returns empty view, never throws.
  // An oversized payload against a small ring is the deterministic
  // allocation-failure path.
  SharedRing ring(16);
  static const char big[1024] = {0};
  Envelope e{1u, 1u, 1u, 1024u};
  std::string_view placed{"x", 1};
  bool threw = false;
  try {
    placed = ring.place(e, std::string_view(big, sizeof(big)));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(placed.empty());
  CHECK(placed.size() == 0u);
  return g_failed_checks == 0;
}

bool check_consume_evicted_is_empty() {
  // Consume of an evicted token returns empty view, never throws.
  SharedRing ring(4);
  const char m[] = {'x', 'y'};
  Envelope e{1u, 1u, 1u, 2u};
  bool threw = false;
  try {
    for (int i = 0; i < 10; ++i) {
      (void)ring.place(e, std::string_view(m, 2));
    }
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  std::string_view got;
  try {
    got = ring.consume(0);  // long evicted
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  CHECK(got.empty());
  return g_failed_checks == 0;
}

bool check_stream_10k_in_order_zero_alloc() {
  // TC-CORE-013 / AC-CORE-013: 10,000-message stream over a pre-sized
  // ring shows zero per-message heap allocations after setup and
  // in-order consume.
  constexpr std::size_t kMessages = 10000;
  constexpr std::size_t kPayload = 8;
  SharedRing ring(kMessages * kPayload);
  static const char payload[kPayload] = {'0', '1', '2', '3',
                                         '4', '5', '6', '7'};
  Envelope e{9u, 2u, 4u, kPayload};
  g_alloc_count = 0;
  g_counting = true;
  bool threw = false;
  try {
    for (std::size_t i = 0; i < kMessages; ++i) {
      (void)ring.place(e, std::string_view(payload, kPayload));
    }
    for (std::size_t i = 0; i < kMessages; ++i) {
      std::string_view got = ring.consume(static_cast<uint64_t>(i));
      if (got.size() != kPayload) {
        g_counting = false;
        std::printf("    message %zu size mismatch\n", i);
        return false;
      }
      if (got != std::string_view(payload, kPayload)) {
        g_counting = false;
        std::printf("    message %zu content mismatch\n", i);
        return false;
      }
    }
  } catch (...) {
    threw = true;
  }
  g_counting = false;
  CHECK(!threw);
  if (threw) return false;
  CHECK(g_alloc_count == 0);
  if (g_alloc_count != 0) {
    std::printf("    %ld heap allocations during 10k stream\n",
                g_alloc_count);
  }
  return g_failed_checks == 0;
}

bool check_place_consume_never_throw_extremes() {
  // Boundary tokens/values never throw: token max, empty payload.
  SharedRing ring(64);
  Envelope e{0xFFFFFFFFu, 0xFFFFu, 0xFFFFu, 0u};
  bool threw = false;
  try {
    std::string_view placed =
        ring.place(e, std::string_view(nullptr, 0));
    (void)placed;
    std::string_view got = ring.consume(0xFFFFFFFFFFFFFFFFull);
    CHECK(got.empty());
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
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
  RUN_CASE(check_place_consume_roundtrip);
  RUN_CASE(check_consume_never_placed_is_empty);
  RUN_CASE(check_tokens_are_insertion_indices);
  RUN_CASE(check_wrap_evicts_oldest);
  RUN_CASE(check_capacity_counts_payload_bytes);
  RUN_CASE(check_place_allocation_failure_empty_never_throws);
  RUN_CASE(check_consume_evicted_is_empty);
  RUN_CASE(check_stream_10k_in_order_zero_alloc);
  RUN_CASE(check_place_consume_never_throw_extremes);
  if (failures > 0) {
    std::printf("ring_buffer_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ring_buffer_test: all passing\n");
  return 0;
}
