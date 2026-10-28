#pragma once

// FR-TEXT-001 / AC-TEXT-002: UAX #9 bidi + UAX #14 line-break entry points.
// Implemented with inline tables (no ICU on host). Both entry points
// validate strict UTF-8 and never throw: malformed/truncated input yields
// empty runs/offsets with Status::kInvalidInput.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "muse/text/text_types.h"

namespace muse::text {

// One directional run in *visual* order.
struct BidiRun {
  std::size_t byte_start = 0;
  std::size_t byte_end = 0;
  // UAX #9 embedding level: even = LTR, odd = RTL.
  std::uint8_t level = 0;
};

struct BidiRuns {
  std::vector<BidiRun> runs;
  Status status = Status::kOk;
};

struct BreakOpportunities {
  // Byte offsets where a line break is allowed (UAX #14-inspired).
  std::vector<std::size_t> offsets;
  Status status = Status::kOk;
};

// Resolves the paragraph into visual-order runs with embedding levels.
// baseDir: 0 = LTR paragraph, 1 = RTL paragraph (overrides implicit dir).
// Bad UTF-8 (or null input with len > 0) -> empty runs + kInvalidInput.
BidiRuns resolveBidi(const std::uint8_t* utf8, std::size_t len,
                     std::uint8_t baseDir) noexcept;

// UAX #14-inspired break offsets for the UTF-8 text. `locale` selects
// locale-specific tailoring where applicable (currently advisory).
// Bad UTF-8 (or null input with len > 0) -> empty offsets + kInvalidInput.
BreakOpportunities findLineBreaks(const std::uint8_t* utf8, std::size_t len,
                                  const std::string& locale) noexcept;

struct BidiParagraph {
  std::vector<BidiRun> runs;
};

struct BreakList {
  std::vector<std::size_t> offsets;
};

StatusOr<BidiParagraph> resolveBidiParagraph(const std::uint8_t* bytes,
                                             std::size_t len,
                                             std::uint8_t base_dir) noexcept;

StatusOr<BreakList> computeBreaks(const std::uint8_t* bytes, std::size_t len,
                                  const char* bcp47_locale) noexcept;

}  // namespace muse::text
