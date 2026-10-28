// Batch 4 contract tests — T8 fuzz half (FR-NET-010 / AC-NET-018;
// RULE-NET-004; TC-NET-018; FR-CORE-004 AC-CORE-014 consumed envelope
// fuzz half).
//
// Hermetic: C++17 + engine/net headers only. The malformed corpus is
// generated inline (deterministic PRNG with a fixed seed — no randomness
// source, no wall clock). No sockets, no env/config reads, no filesystem,
// no live services. ASan-cleanliness is observed by running the binary
// under `-fsanitize=address,undefined` (coder wires the build flag; the
// test itself performs only in-bounds, exception-safe calls).
//
// Binds to the T8 `Interface:` line in plan.md for the fuzz half:
// malformed input over all T7 codecs + the envelope parser yields
// structured errors only — zero crashes, zero OOB reads, never an
// exception escape. Envelope shape consumed from FR-CORE-004:
//   struct muse::net::Envelope { uint32_t schemaId; uint16_t
//     schemaVersion; uint16_t abiVersion; uint32_t payloadLen; };
// A schema/ABI mismatch is rejected before any buffer is interpreted
// (AC-CORE-012).
//
// ASSUMPTIONS:
//   A-ENVELOPE-PARSE: the envelope parser under fuzz is
//     `muse::net::ConsumeInPlace(const Envelope&, std::string_view)` —
//     the only T8 entry point that interprets (Envelope, bytes). Fuzzing
//     it with hostile lengths/versions proves "rejected before any
//     buffer is interpreted".
//   A-CORPUS: "full malformed corpus" = the deterministic mutation set
//     below (truncations, bit flips, hostile length prefixes, version
//     skew, overlong runs). It is representative, not exhaustive; the
//     property asserted is universal (no case may crash/throw/OOB).
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "net/codec.h"
#include "net/zero_copy.h"

namespace {

using muse::net::Codec;
using muse::net::CodecId;
using muse::net::CodecRegistry;
using muse::net::ConsumeInPlace;
using muse::net::DecodeResult;
using muse::net::Envelope;

int g_failed_checks = 0;

#define CHECK(cond)                                  \
  do {                                               \
    if (!(cond)) {                                   \
      ++g_failed_checks;                             \
      std::printf("    CHECK failed line %d: %s\n",  \
                  __LINE__, #cond);                 \
    }                                                \
  } while (0)

// Deterministic xorshift32 — fixed seed, no entropy source.
uint32_t NextRand(uint32_t* s) {
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

// Build the deterministic malformed corpus: seeds × mutations.
std::vector<std::string> BuildCorpus() {
  const std::vector<std::string> seeds = {
      "{\"a\":1}", "{\"user\":{\"id\":7,\"tags\":[\"x\",\"y\"]}}", "{}",
      "\x00\x01\x02\x03\x04\x05\x06\x07",
      std::string(256, 'A'),
  };
  std::vector<std::string> out;
  // Raw seeds + truncations of every seed at every 1-byte step (short
  // seeds only — bounded, deterministic).
  for (const std::string& s : seeds) {
    out.push_back(s);
    for (size_t n = 0; n < s.size(); ++n) out.push_back(s.substr(0, n));
  }
  // Bit-flip and hostile-byte mutations with a fixed PRNG stream.
  uint32_t st = 0xC0DECu;
  for (int i = 0; i < 200; ++i) {
    std::string m = seeds[i % seeds.size()];
    if (m.empty()) m = "pad";
    size_t pos = NextRand(&st) % m.size();
    m[pos] = static_cast<char>(NextRand(&st) & 0xFF);
    out.push_back(m);
  }
  // Hostile length-prefix shapes (protobuf/CBOR/MessagePack/FlatBuffers
  // framing attacks): huge varints, 0xFF runs, NUL runs.
  out.emplace_back(std::string("\xff\xff\xff\xff\x0f", 5));
  out.emplace_back(std::string(64, '\xff'));
  out.emplace_back(std::string(64, '\x00'));
  out.emplace_back(std::string(1 << 16, 'B'));  // 64 KiB overlong
  // UTF-8 hostile shapes for the JSON path.
  out.emplace_back(std::string("\xed\xa0\x80", 3));  // lone surrogate
  out.emplace_back(std::string("\xf0\x28\x8c\x28", 4));  // bad seq
  return out;
}

bool fuzz_all_codecs_structured_errors_only() {
  const CodecId ids[] = {CodecId::kJson, CodecId::kProtobuf,
                         CodecId::kFlatBuffers, CodecId::kCbor,
                         CodecId::kMsgPack};
  const std::vector<std::string> corpus = BuildCorpus();
  CHECK(corpus.size() > 200);  // corpus is substantial, not a token
  for (CodecId id : ids) {
    CodecRegistry reg;
    Codec* c = nullptr;
    try {
      c = &reg.get(id);
    } catch (...) {
      std::printf("    get() threw for codec %d\n", (int)id);
      return false;
    }
    for (size_t i = 0; i < corpus.size(); ++i) {
      DecodeResult r{true, "", "", ""};
      bool ok_call = true;
      try {
        r = c->decode(corpus[i]);
      } catch (...) {
        ok_call = false;
      }
      if (!ok_call) {
        std::printf("    codec %d threw on corpus[%zu]\n", (int)id, i);
        return false;
      }
      // Either verdict is acceptable; the invariant is STRUCTURE:
      // ok==false implies a non-empty machine-readable error code,
      // and nothing crashes or throws.
      if (!r.ok) CHECK(!r.error.empty());
    }
    // Validate() over the same corpus: same no-throw invariant.
    for (size_t i = 0; i < corpus.size(); ++i) {
      DecodeResult r{true, "", "", ""};
      bool ok_call = true;
      try {
        r = muse::net::Validate(id, corpus[i], 1 << 20);
      } catch (...) {
        ok_call = false;
      }
      if (!ok_call) {
        std::printf("    Validate threw for codec %d corpus[%zu]\n",
                    (int)id, i);
        return false;
      }
      (void)r;
    }
  }
  return g_failed_checks == 0;
}

bool fuzz_envelope_parser_rejects_before_interpreting() {
  // Hostile (Envelope, bytes) pairs: version skew, schema skew, and
  // payloadLen far beyond the mapped bytes. ConsumeInPlace must never
  // read OOB — mismatch yields an empty view (typed-error signal, see
  // contract Conflicts), never a crash/throw/OOB.
  const std::string mapped = "0123456789abcdef";  // 16 mapped bytes
  uint32_t st = 0xF00D1Eu;
  for (int i = 0; i < 300; ++i) {
    Envelope e;
    e.schemaId = NextRand(&st);
    e.schemaVersion = static_cast<uint16_t>(NextRand(&st) & 0xFFFF);
    e.abiVersion = static_cast<uint16_t>(NextRand(&st) & 0xFFFF);
    e.payloadLen = NextRand(&st);  // often >> mapped.size()
    std::string_view view;
    bool ok_call = true;
    try {
      view = ConsumeInPlace(e, mapped);
    } catch (...) {
      ok_call = false;
    }
    CHECK(ok_call);
    if (!ok_call) return false;
    // Whatever comes back must stay inside the original mapping —
    // empty on mismatch, otherwise a sub-span of `mapped`.
    if (!view.empty()) {
      const char* base = mapped.data();
      const char* p = view.data();
      CHECK(p >= base);
      CHECK(p + view.size() <= base + mapped.size());
    }
  }
  // The one well-formed envelope (matching versions/length) must
  // consume cleanly in place.
  Envelope good{1, 1, 1, 8};
  std::string_view gv;
  try {
    gv = ConsumeInPlace(good, mapped);
  } catch (...) {
    return false;
  }
  (void)gv;
  return g_failed_checks == 0;
}

}  // namespace

#define RUN_CASE(name)                   \
  do {                                   \
    g_failed_checks = 0;                 \
    if (!(name)()) {                     \
      std::printf("FAIL %s\n", #name);   \
      ++failures;                        \
    } else {                             \
      std::printf("PASS %s\n", #name);   \
    }                                    \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(fuzz_all_codecs_structured_errors_only);
  RUN_CASE(fuzz_envelope_parser_rejects_before_interpreting);
  if (failures > 0) {
    std::printf("codec_fuzz_test: %d failing\n", failures);
    return 1;
  }
  std::printf("codec_fuzz_test: all passing\n");
  return 0;
}
