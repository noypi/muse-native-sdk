// Batch 2 contract tests — T4 fuzz + 10k stream + in-place FlatBuffers
// consume (FR-CORE-004 / AC-CORE-014; TC-CORE-014).
//
// Hermetic: C++17 + engine/core/bridge headers only. No sockets, no
// env/config reads, no filesystem, no live services, no wall-clock.
// The fuzz corpus is a seeded deterministic LCG (fixed seed, no RNG
// device, no time); payloads are inline byte arrays. ASan/UBSan run
// outside this file (build flags); here every mutated input asserts
// typed errors with no over-read by construction (short views simply
// lack the bytes).
//
// Binds to the T4 `Interface:` line in plan.md:
//   muse::bridge::ConsumeInPlace(const Envelope&, std::string_view
//     mapped) noexcept -> std::string_view;
//   mismatch or payloadLen>mapped.size() returns empty view;
//   never throws, never reads OOB, performs zero heap allocation.
//
// ASSUMPTIONS:
//   A1: new module mirroring engine/net ConsumeInPlace shape (plan A1).
//   A2: gradlew names stale, CTest used (plan A2).
//   A3: ring is process memory with the same contract (plan A3); the
//     10k-stream case below runs through muse::bridge::SharedRing and
//     observes zero per-message heap allocations with the instrumented
//     operator new counter (same A-ALLOC technique as ring_buffer_test:
//     setup may allocate, the measured stream must not).
//   A-VIEW: the happy-path view is a sub-span of the mapping —
//     view.data() lies inside [mapped.data(), mapped.data()+size) and
//     view.size() == envelope.payloadLen with identical bytes. The
//     Interface line does not pin the sub-span offset; tests assert
//     range containment + content equality (no copy), never an exact
//     offset.
//   A-FUZZ: the seeded corpus below (LCG seed 0xC0RE) stands in for the
//     plan's fuzz-gate block; build-level ASan/UBSan flags are the
//     coder's CMake wiring, this file only feeds mutated inputs and
//     asserts typed, throw-free, OOB-free outcomes.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#include "core/bridge/consume.h"
#include "core/bridge/envelope.h"
#include "core/bridge/ring_buffer.h"

// --- Allocation instrumentation (A-ALLOC, same as ring_buffer_test) ---
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

using muse::bridge::CheckEnvelope;
using muse::bridge::ConsumeInPlace;
using muse::bridge::DecodeEnvelope;
using muse::bridge::EncodeEnvelope;
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

// Deterministic LCG (A-FUZZ): no device, no time, fixed seed.
static uint64_t g_lcg = 0xC0EE004ULL;
uint64_t NextRand() {
  g_lcg = g_lcg * 6364136223846793005ULL + 1442695040888963407ULL;
  return (g_lcg >> 33);
}

bool check_consume_happy_path_is_subspan() {
  // Happy path: matching envelope + sufficient mapping returns a
  // sub-span view into the original mapping (no copy).
  static const char kPayload[] = {'F', 'B', '0', '1',
                                  '2', '3', '4', '5'};
  constexpr size_t kLen = sizeof(kPayload);
  // Mapping holds the payload bytes (in-place contract).
  static char mapped[64] = {0};
  for (size_t i = 0; i < kLen; ++i) mapped[i] = kPayload[i];
  Envelope e{7u, 2u, 3u, kLen};
  std::string_view mapped_view(mapped, sizeof(mapped));
  std::string_view got;
  bool threw = false;
  try {
    got = ConsumeInPlace(e, mapped_view);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!got.empty());
  CHECK(got.size() == kLen);
  // A-VIEW: containment + content, not exact offset.
  CHECK(got.data() >= mapped_view.data());
  CHECK(got.data() + got.size() <= mapped_view.data() + mapped_view.size());
  CHECK(got == std::string_view(kPayload, kLen));
  return g_failed_checks == 0;
}

bool check_consume_mismatch_returns_empty() {
  // Mismatched envelope identity is rejected before interpretation:
  // empty view. (CheckEnvelope gate mirrored at consume level — the
  // consume contract takes the envelope as already-decoded header.)
  static const char kPayload[] = {'a', 'b', 'c', 'd'};
  std::string_view mapped(kPayload, sizeof(kPayload));
  // payloadLen claims more than mapped holds AND schema differs;
  // either way the contract demands empty.
  Envelope wrong_schema{999u, 2u, 3u, 4u};
  Envelope wrong_version{7u, 99u, 3u, 4u};
  Envelope wrong_abi{7u, 2u, 99u, 4u};
  bool threw = false;
  try {
    // Gate proof: CheckEnvelope rejects each variant with its typed
    // code, and ConsumeInPlace yields empty for the too-long view.
    CHECK(!CheckEnvelope(wrong_schema, 7u, 2u, 3u).ok);
    CHECK(!CheckEnvelope(wrong_version, 7u, 2u, 3u).ok);
    CHECK(!CheckEnvelope(wrong_abi, 7u, 2u, 3u).ok);
    Envelope too_long{7u, 2u, 3u, 4096u};
    CHECK(ConsumeInPlace(too_long, mapped).empty());
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  return g_failed_checks == 0;
}

bool check_consume_payload_len_overflow_empty_no_oob() {
  // payloadLen > mapped.size() returns empty, never throws, never
  // reads OOB (the bytes are not there; ASan would flag any over-read
  // in a sanitizer build).
  static const char kSmall[] = {'x', 'y'};
  std::string_view mapped(kSmall, sizeof(kSmall));
  Envelope e{1u, 1u, 1u, 1u << 20};
  std::string_view got("sentinel", 8);
  bool threw = false;
  try {
    got = ConsumeInPlace(e, mapped);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(got.empty());
  // Empty mapping is the degenerate overflow.
  try {
    got = ConsumeInPlace(Envelope{1u, 1u, 1u, 1u},
                         std::string_view(nullptr, 0));
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  CHECK(got.empty());
  return g_failed_checks == 0;
}

bool check_consume_zero_alloc_no_throw() {
  // ConsumeInPlace performs zero heap allocation and never throws.
  static char mapped[256] = {0};
  for (int i = 0; i < 256; ++i) mapped[i] = static_cast<char>(i);
  Envelope e{3u, 1u, 1u, 128u};
  std::string_view mapped_view(mapped, sizeof(mapped));
  g_alloc_count = 0;
  g_counting = true;
  bool threw = false;
  long total = 0;
  try {
    for (int i = 0; i < 1000; ++i) {
      std::string_view got = ConsumeInPlace(e, mapped_view);
      // Touch the result so the call cannot be elided, without
      // allocating: fold bytes into a sink.
      volatile size_t sink = got.size();
      (void)sink;
      if (got.size() != 128u) {
        g_counting = false;
        std::printf("    iteration %d size mismatch\n", i);
        return false;
      }
    }
  } catch (...) {
    threw = true;
  }
  total = g_alloc_count;
  g_counting = false;
  CHECK(!threw);
  if (threw) return false;
  CHECK(total == 0);
  if (total != 0) {
    std::printf("    %ld heap allocations during 1000 consumes\n", total);
  }
  return g_failed_checks == 0;
}

bool check_fuzz_envelope_mutations_typed_errors_only() {
  // A-FUZZ: mutated schemaId/schemaVersion/abiVersion/payloadLen over
  // Encode->Decode yields only typed outcomes: either a faithful round
  // trip or a documented error code; never throws, never OOB.
  constexpr int kIters = 5000;
  int round_trips = 0;
  int typed_errors = 0;
  bool threw = false;
  try {
    for (int i = 0; i < kIters; ++i) {
      Envelope e{
          static_cast<uint32_t>(NextRand()),
          static_cast<uint16_t>(NextRand()),
          static_cast<uint16_t>(NextRand()),
          static_cast<uint32_t>(NextRand() % (128u * 1024u)),
      };
      std::string bytes = EncodeEnvelope(e);
      auto out = DecodeEnvelope(bytes);
      if (out.ok) {
        ++round_trips;
        CHECK(out.envelope.schemaId == e.schemaId);
        CHECK(out.envelope.schemaVersion == e.schemaVersion);
        CHECK(out.envelope.abiVersion == e.abiVersion);
        CHECK(out.envelope.payloadLen == e.payloadLen);
        // Consume with a too-small mapping must be empty (OOB-safe).
        std::string_view tiny(bytes.data(), bytes.size());
        std::string_view got = ConsumeInPlace(out.envelope, tiny);
        // payloadLen routinely exceeds the header-only view: empty.
        if (e.payloadLen > tiny.size()) CHECK(got.empty());
      } else {
        ++typed_errors;
        CHECK(out.error == "truncated" || out.error == "bad-magic" ||
              out.error == "length-overflow");
      }
      // Byte-level mutation of the header: still typed, still no throw.
      if (!bytes.empty() && (NextRand() % 4u) == 0u) {
        bytes[static_cast<size_t>(NextRand() % bytes.size())] ^=
            static_cast<char>(1 + NextRand() % 255u);
        auto m = DecodeEnvelope(bytes);
        if (!m.ok) {
          CHECK(m.error == "truncated" || m.error == "bad-magic" ||
                m.error == "length-overflow");
        }
      }
    }
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(round_trips > 0);
  CHECK(round_trips + typed_errors >= kIters);
  std::printf("    fuzz: %d round trips, %d typed errors / %d iters\n",
              round_trips, typed_errors, kIters);
  return g_failed_checks == 0;
}

bool check_stream_10k_through_ring_zero_alloc() {
  // TC-CORE-014 / AC-CORE-014: 10k-payload stream through the T3 ring
  // with allocation counter; in-order consume, zero per-message heap
  // allocations after setup.
  constexpr std::size_t kMessages = 10000;
  constexpr std::size_t kPayload = 16;
  SharedRing ring(kMessages * kPayload);
  static const char payload[kPayload] = {'F', 'L', 'A', 'T', 'B', 'U',
                                         'F', '0', '1', '2', '3', '4',
                                         '5', '6', '7', '8'};
  Envelope e{21u, 1u, 2u, kPayload};
  g_alloc_count = 0;
  g_counting = true;
  bool threw = false;
  try {
    for (std::size_t i = 0; i < kMessages; ++i) {
      (void)ring.place(e, std::string_view(payload, kPayload));
    }
    for (std::size_t i = 0; i < kMessages; ++i) {
      std::string_view got = ring.consume(static_cast<uint64_t>(i));
      if (got != std::string_view(payload, kPayload)) {
        g_counting = false;
        std::printf("    stream message %zu mismatch\n", i);
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

bool check_flatbuffers_consume_no_copy() {
  // FlatBuffers payload consumed via sub-span view: data() lies inside
  // the original mapping (no deserialize copy) with identical bytes.
  // The fake table below stands in for a FlatBuffers-verified buffer;
  // the contract under test is the zero-copy view, not the schema.
  static const char kTable[] = {
      0x10, 0x00, 0x00, 0x00,  // fake vtable offset
      0x00, 0x00, 0x0A, 0x00,  // fake field scaffolding
      'M', 'U', 'S', 'E', 'F', 'B', 0x00, 0x00,
  };
  constexpr size_t kLen = sizeof(kTable);
  static char mapped[128] = {0};
  for (size_t i = 0; i < kLen; ++i) mapped[i] = kTable[i];
  std::string_view mapped_view(mapped, sizeof(mapped));
  Envelope e{77u, 4u, 5u, kLen};
  std::string_view got;
  bool threw = false;
  try {
    got = ConsumeInPlace(e, mapped_view);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!got.empty());
  CHECK(got.size() == kLen);
  CHECK(got.data() >= mapped_view.data());
  CHECK(got.data() + got.size() <= mapped_view.data() + mapped_view.size());
  CHECK(got == std::string_view(kTable, kLen));
  // Pointer-equality proof of no copy: the view aliases the mapping.
  bool aliases = false;
  for (size_t off = 0; off + kLen <= sizeof(mapped); ++off) {
    if (got.data() == mapped + off) {
      aliases = true;
      break;
    }
  }
  CHECK(aliases);
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
  RUN_CASE(check_consume_happy_path_is_subspan);
  RUN_CASE(check_consume_mismatch_returns_empty);
  RUN_CASE(check_consume_payload_len_overflow_empty_no_oob);
  RUN_CASE(check_consume_zero_alloc_no_throw);
  RUN_CASE(check_fuzz_envelope_mutations_typed_errors_only);
  RUN_CASE(check_stream_10k_through_ring_zero_alloc);
  RUN_CASE(check_flatbuffers_consume_no_copy);
  if (failures > 0) {
    std::printf("bridge_fuzz_test: %d failing\n", failures);
    return 1;
  }
  std::printf("bridge_fuzz_test: all passing\n");
  return 0;
}
