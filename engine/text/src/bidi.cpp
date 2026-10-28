// FR-TEXT-001 / AC-TEXT-002: UAX #9-inspired bidi + UAX #14-inspired breaks.
// No ICU on host: real inline-table logic (strict UTF-8 validation,
// explicit script ranges, RTL run splitting with embedding levels, break
// rules for spaces / CJK wide chars / SE-Asian clusters). Never throws.

#include "muse/text/bidi.h"

#include <cstdint>
#include <vector>

namespace muse::text {
namespace {

// --- Strict UTF-8 decoding (same discipline as the T1 shaper core). ---
// Decodes the sequence starting at data[pos]; false on malformed,
// truncated, over-long, surrogate, or out-of-range input.
bool FetchCodepoint(const std::uint8_t* data, std::size_t len,
                    std::size_t pos, std::uint32_t* out_cp,
                    std::size_t* out_n) noexcept {
  const std::uint8_t b0 = data[pos];
  if (b0 < 0x80) {
    *out_cp = b0;
    *out_n = 1;
    return true;
  }
  std::size_t need = 0;
  std::uint32_t cp = 0;
  std::uint32_t lo = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    need = 2;
    cp = static_cast<std::uint32_t>(b0 & 0x1F);
    lo = 0x80;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    need = 3;
    cp = static_cast<std::uint32_t>(b0 & 0x0F);
    lo = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    need = 4;
    cp = static_cast<std::uint32_t>(b0 & 0x07);
    lo = 0x10000;
  } else {
    return false;
  }
  if (pos + need > len) return false;
  for (std::size_t i = 1; i < need; ++i) {
    const std::uint8_t c = data[pos + i];
    if ((c & 0xC0) != 0x80) return false;
    cp = (cp << 6) | static_cast<std::uint32_t>(c & 0x3F);
  }
  if (cp < lo || cp > 0x10FFFF) return false;
  if (cp >= 0xD800 && cp <= 0xDFFF) return false;
  *out_cp = cp;
  *out_n = need;
  return true;
}

struct Decoded {
  std::uint32_t cp = 0;
  std::size_t byte_start = 0;
  std::size_t byte_len = 1;
};

// Decodes the whole buffer; false on any bad sequence.
bool DecodeAll(const std::uint8_t* data, std::size_t len,
               std::vector<Decoded>* out) noexcept {
  out->clear();
  std::size_t pos = 0;
  while (pos < len) {
    std::uint32_t cp = 0;
    std::size_t n = 0;
    if (!FetchCodepoint(data, len, pos, &cp, &n)) return false;
    out->push_back(Decoded{cp, pos, n});
    pos += n;
  }
  return true;
}

enum class Script : std::uint8_t {
  kOther = 0,
  kHebrew,
  kArabic,
  kDevanagari,
  kIndic,  // Other Indic blocks + Sinhala.
  kThai,
  kLao,
  kKhmer,
  kMyanmar,
  kCjk,      // Han ideographs.
  kKana,     // Hiragana / Katakana.
  kHangul,   // Korean Hangul.
};

// Explicit, spot-checkable script ranges (Unicode 15 block boundaries).
Script ClassifyScript(std::uint32_t cp) noexcept {
  if (cp >= 0x0590 && cp <= 0x05FF) return Script::kHebrew;
  if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
      (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
      (cp >= 0xFE70 && cp <= 0xFEFF))
    return Script::kArabic;
  if (cp >= 0x0900 && cp <= 0x097F) return Script::kDevanagari;
  // Other Indic blocks + Sinhala (Bengali..Malayalam, Sinhala).
  if ((cp >= 0x0980 && cp <= 0x0D7F) || (cp >= 0x0D80 && cp <= 0x0DFF))
    return Script::kIndic;
  if (cp >= 0x0E00 && cp <= 0x0E7F) return Script::kThai;
  if (cp >= 0x0E80 && cp <= 0x0EFF) return Script::kLao;
  if (cp >= 0x1780 && cp <= 0x17FF) return Script::kKhmer;
  if ((cp >= 0x1000 && cp <= 0x109F) || (cp >= 0xA9E0 && cp <= 0xA9FF) ||
      (cp >= 0xAA60 && cp <= 0xAA7F))
    return Script::kMyanmar;
  if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
      (cp >= 0x20000 && cp <= 0x2A6DF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0x2F800 && cp <= 0x2FA1F))
    return Script::kCjk;
  if ((cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x30A0 && cp <= 0x30FF) ||
      (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF61 && cp <= 0xFFDC))
    return Script::kKana;
  if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) ||
      (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xD7B0 && cp <= 0xD7FF))
    return Script::kHangul;
  return Script::kOther;
}

bool IsRtlScript(Script s) noexcept {
  return s == Script::kHebrew || s == Script::kArabic;
}

bool IsCjkWide(Script s) noexcept {
  return s == Script::kCjk || s == Script::kKana || s == Script::kHangul;
}

// SE-Asian scripts whose grapheme clusters must not break internally.
bool IsNoBreakClusterScript(Script s) noexcept {
  return s == Script::kThai || s == Script::kLao || s == Script::kKhmer ||
         s == Script::kMyanmar;
}

// UAX #14 LB1: allow break after spaces (and other ASCII blanks).
bool IsSpaceBreak(std::uint32_t cp) noexcept {
  return cp == 0x20 || cp == 0x09;
}

}  // namespace

BidiRuns resolveBidi(const std::uint8_t* utf8, std::size_t len,
                     std::uint8_t baseDir) noexcept {
  try {
    BidiRuns out;
    if (len == 0) {
      out.status = Status::kOk;
      return out;
    }
    if (utf8 == nullptr) {
      out.status = Status::kInvalidInput;
      return out;
    }
    std::vector<Decoded> chars;
    if (!DecodeAll(utf8, len, &chars)) {
      BidiRuns bad;
      bad.status = Status::kInvalidInput;
      return bad;
    }
    const bool para_rtl = (baseDir != 0);

    // Split into directional runs: strong RTL chars (Hebrew/Arabic) form
    // RTL runs; everything else (incl. neutrals, attached to the current
    // run) forms LTR runs.
    struct Run {
      std::size_t byte_start;
      std::size_t byte_end;
      bool rtl;
    };
    std::vector<Run> logical;
    for (const Decoded& d : chars) {
      const bool rtl = IsRtlScript(ClassifyScript(d.cp));
      if (logical.empty() || logical.back().rtl != rtl) {
        logical.push_back(Run{d.byte_start, d.byte_start + d.byte_len, rtl});
      } else {
        logical.back().byte_end = d.byte_start + d.byte_len;
      }
    }
    // Levels: LTR paragraph -> LTR=0, RTL=1; RTL paragraph -> RTL=1, LTR=2.
    // Visual order: LTR paragraph keeps logical order, RTL paragraph
    // reverses run order (UAX #9 I1/I2 reordering, simplified to run level).
    if (para_rtl) {
      for (auto it = logical.rbegin(); it != logical.rend(); ++it) {
        BidiRun r;
        r.byte_start = it->byte_start;
        r.byte_end = it->byte_end;
        r.level = it->rtl ? 1 : 2;
        out.runs.push_back(r);
      }
    } else {
      for (const Run& r : logical) {
        BidiRun v;
        v.byte_start = r.byte_start;
        v.byte_end = r.byte_end;
        v.level = r.rtl ? 1 : 0;
        out.runs.push_back(v);
      }
    }
    out.status = Status::kOk;
    return out;
  } catch (...) {
    BidiRuns bad;
    bad.status = Status::kInvalidInput;
    return bad;
  }
}

BreakOpportunities findLineBreaks(const std::uint8_t* utf8, std::size_t len,
                                  const std::string& locale) noexcept {
  (void)locale;  // Advisory only; core UAX #14-inspired rules are universal.
  try {
    BreakOpportunities out;
    if (len == 0) {
      out.status = Status::kOk;
      return out;
    }
    if (utf8 == nullptr) {
      out.status = Status::kInvalidInput;
      return out;
    }
    std::vector<Decoded> chars;
    if (!DecodeAll(utf8, len, &chars)) {
      BreakOpportunities bad;
      bad.status = Status::kInvalidInput;
      return bad;
    }
    const std::size_t n = chars.size();
    std::vector<bool> allow_before(n + 1, false);
    // LB: break after every space (SP); UAX #14 LB7-ish (spaces don't
    // break before, break after).
    for (std::size_t i = 0; i < n; ++i) {
      if (IsSpaceBreak(chars[i].cp)) allow_before[i + 1] = true;
    }
    // UAX #14 LB18-ish: break before and after each CJK wide char
    // (ideographs, kana, hangul syllables behave as ID).
    for (std::size_t i = 0; i < n; ++i) {
      if (IsCjkWide(ClassifyScript(chars[i].cp))) {
        if (i > 0) allow_before[i] = true;
        allow_before[i + 1] = true;
      }
    }
    // Final position (end of text) is always a valid break.
    allow_before[n] = true;
    // UAX #14 + Thai/Lao/Khmer/Myanmar syllable discipline: never break
    // strictly inside a run of those cluster scripts; also keep Indic
    // conjuncts (virama-joined) unbroken.
    for (std::size_t i = 0; i <= n; ++i) {
      if (!allow_before[i]) continue;
      if (i == 0 || i == n) continue;  // 0 excluded; end handled below.
      const Script prev = ClassifyScript(chars[i - 1].cp);
      const Script next = ClassifyScript(chars[i].cp);
      if (IsNoBreakClusterScript(prev) && IsNoBreakClusterScript(next)) {
        allow_before[i] = false;
        continue;
      }
      // Indic conjunct: X + virama (U+094D et al.) + X stays unbroken.
      if ((chars[i - 1].cp == 0x094D || chars[i].cp == 0x094D) &&
          (prev == Script::kDevanagari || prev == Script::kIndic ||
           next == Script::kDevanagari || next == Script::kIndic)) {
        allow_before[i] = false;
        continue;
      }
    }
    // Rebuild offsets from boundary indices.
    out.offsets.clear();
    for (std::size_t i = 1; i <= n; ++i) {
      if (!allow_before[i]) continue;
      const std::size_t off = (i == n) ? len : chars[i].byte_start;
      out.offsets.push_back(off);
    }
    out.status = Status::kOk;
    return out;
  } catch (...) {
    BreakOpportunities bad;
    bad.status = Status::kInvalidInput;
    return bad;
  }
}

}  // namespace muse::text
