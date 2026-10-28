#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "runtime/composition_roundtrip.h"
#include "runtime/gap_buffer.h"

// Rich clipboard copy/paste (FR-TEXT-013 / AC-TEXT-035). Hermetic: payloads
// are plain in-memory structs; no OS clipboard, no network, no filesystem.
// Out-of-range [lo, hi) clamps to buffer bounds; empty payload paste is a
// no-op; paste inserts (shifts text right, never overwrites). Never throws.
struct ClipPayload {
  std::string plain;
  std::string html;
  bool has_image = false;
};

inline ClipPayload copy_rich(const GapBuffer& buf, size_t lo, size_t hi) noexcept {
  try {
    const size_t size = buf.byte_size();
    lo = (std::min)(lo, size);
    hi = (std::min)(hi, size);
    ClipPayload p;
    if (lo >= hi) {
      return p;
    }
    const std::string text = buf.to_string();
    p.plain = text.substr(lo, hi - lo);
    p.has_image = false;
    // Serialize each span kind overlapping [lo, hi) as a flat
    // <span data-kind="K">...</span> wrapper. GapBuffer exposes spans only
    // via spans_of_kind(kind), so probe the small-kind domain plus the
    // known composing kind; kinds outside that domain are not serialized
    // (plain still carries the text).
    struct Seg {
      size_t lo = 0;
      size_t hi = 0;
      uint32_t kind = 0;
    };
    std::vector<Seg> segs;
    const uint32_t kComposingKind = 0x434F4D50u;
    bool composing_probed = false;
    for (uint32_t kind = 0; kind <= 4095; ++kind) {
      if (kind == kComposingKind) {
        composing_probed = true;
      }
      std::vector<std::pair<size_t, size_t>> spans;
      try {
        spans = buf.spans_of_kind(kind);
      } catch (...) {
        continue;
      }
      for (const auto& sp : spans) {
        const size_t a = (std::max)(sp.first, lo);
        const size_t b = (std::min)(sp.second, hi);
        if (a < b) {
          segs.push_back(Seg{a - lo, b - lo, kind});
        }
      }
      if (kind == 4095) {
        break;  // Avoid wrapping the uint32_t loop bound.
      }
    }
    if (!composing_probed) {
      std::vector<std::pair<size_t, size_t>> spans;
      try {
        spans = buf.spans_of_kind(kComposingKind);
      } catch (...) {
      }
      for (const auto& sp : spans) {
        const size_t a = (std::max)(sp.first, lo);
        const size_t b = (std::min)(sp.second, hi);
        if (a < b) {
          segs.push_back(Seg{a - lo, b - lo, kComposingKind});
        }
      }
    }
    if (segs.empty()) {
      p.html = p.plain;
      return p;
    }
    std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) {
      if (a.lo != b.lo) {
        return a.lo < b.lo;
      }
      if (a.hi != b.hi) {
        return a.hi < b.hi;
      }
      return a.kind < b.kind;
    });
    auto escape = [](const std::string& s) {
      std::string out;
      out.reserve(s.size());
      for (char c : s) {
        switch (c) {
          case '&':
            out += "&amp;";
            break;
          case '<':
            out += "&lt;";
            break;
          case '>':
            out += "&gt;";
            break;
          case '"':
            out += "&quot;";
            break;
          case '\'':
            out += "&#39;";
            break;
          default:
            out += c;
            break;
        }
      }
      return out;
    };
    std::string html;
    html.reserve(p.plain.size() + segs.size() * 32);
    size_t cursor = 0;
    size_t idx = 0;
    while (idx < segs.size()) {
      const Seg& sg = segs[idx];
      if (sg.lo > cursor) {
        html += escape(p.plain.substr(cursor, sg.lo - cursor));
        cursor = sg.lo;
      }
      if (sg.hi <= cursor) {
        ++idx;  // Fully inside an already-emitted wrapper.
        continue;
      }
      // Nest every segment sharing this exact [lo, hi) so no kind is lost.
      size_t end = idx + 1;
      while (end < segs.size() && segs[end].lo == sg.lo &&
             segs[end].hi == sg.hi) {
        ++end;
      }
      for (size_t k = idx; k < end; ++k) {
        html += "<span data-kind=\"";
        html += std::to_string(segs[k].kind);
        html += "\">";
      }
      html += escape(p.plain.substr(cursor, sg.hi - cursor));
      for (size_t k = idx; k < end; ++k) {
        html += "</span>";
      }
      cursor = sg.hi;
      idx = end;
    }
    if (cursor < p.plain.size()) {
      html += escape(p.plain.substr(cursor));
    }
    p.html = html;
    return p;
  } catch (...) {
    return ClipPayload{};
  }
}

inline void paste_rich(GapBuffer& buf, size_t pos, const ClipPayload& p) noexcept {
  try {
    // S5a: refuse oversize payloads (> 4MB plain+html total) — buffer
    // unchanged, no throw (refuse, NOT truncation).
    static constexpr size_t kMaxPasteBytes = 4u * 1024u * 1024u;
    if (p.plain.size() + p.html.size() > kMaxPasteBytes) {
      return;
    }
    if (p.plain.empty()) {
      return;
    }
    pos = (std::min)(pos, buf.byte_size());
    if (p.html.empty()) {
      buf.insert(pos, p.plain);
      return;
    }
    // Parse <span data-kind="K">...</span> wrappers produced by copy_rich;
    // on any parse failure fall back to inserting plain text only.
    const std::string& html = p.html;
    std::string text;
    text.reserve(p.plain.size());
    struct Tag {
      size_t lo = 0;
      size_t hi = 0;
      uint32_t kind = 0;
    };
    std::vector<Tag> tags;
    bool ok = true;
    size_t i = 0;
    // Recursive descent over our own flat/nested <span data-kind="K">
    // wrappers. Returns false on any foreign markup or unbalanced tags.
    struct Parser {
      const std::string& html;
      std::string& text;
      std::vector<Tag>& tags;
      bool& ok;
      size_t& i;
      static std::string unescape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t j = 0; j < s.size();) {
          if (s[j] == '&') {
            if (s.compare(j, 5, "&amp;") == 0) {
              out += '&';
              j += 5;
            } else if (s.compare(j, 4, "&lt;") == 0) {
              out += '<';
              j += 4;
            } else if (s.compare(j, 4, "&gt;") == 0) {
              out += '>';
              j += 4;
            } else if (s.compare(j, 6, "&quot;") == 0) {
              out += '"';
              j += 6;
            } else if (s.compare(j, 5, "&#39;") == 0) {
              out += '\'';
              j += 5;
            } else {
              out += s[j];
              ++j;
            }
          } else {
            out += s[j];
            ++j;
          }
        }
        return out;
      }
      // S1: max nesting depth. Past the cap the parse returns ok=false so
      // the payload falls back to plain-text insert (never recurses per
      // attacker depth).
      static constexpr size_t kMaxNestingDepthFn() { return 64; }
      // S2: pasted span-kind allowlist: 0-4095 excluding the internal
      // composing kind. NOTE: 0x434F4D50u == 1129270608, NOT 1128740432
      // (the review's decimal is 0x43473650, a different kind).
      static bool kind_allowed(const std::string& digits, unsigned long v) {
        if (digits.empty()) {
          return false;
        }
        for (char c : digits) {
          if (c < '0' || c > '9') {
            return false;  // Rejects "-1", "+1", "0x..", whitespace, etc.
          }
        }
        if (v > 4095ul) {
          return false;
        }
        if (static_cast<uint32_t>(v) == detail_composition::kComposingKind) {
          return false;
        }
        return true;
      }
      void parse(bool nested, size_t depth = 0) {
        bool closed = false;
        while (ok && i < html.size()) {
          if (html.compare(i, 5, "<span") == 0 &&
              (i + 5 >= html.size() || html[i + 5] == ' ' ||
               html[i + 5] == '>')) {
            const size_t key = html.find("data-kind=\"", i);
            const size_t open_end = html.find('>', i);
            if (key == std::string::npos || open_end == std::string::npos ||
                key > open_end) {
              ok = false;
              return;
            }
            const size_t num_begin = key + 11;
            const size_t num_end = html.find('"', num_begin);
            if (num_end == std::string::npos || num_end > open_end) {
              ok = false;
              return;
            }
            unsigned long kind = 0;
            std::string digits;
            try {
              digits = html.substr(num_begin, num_end - num_begin);
              kind = std::stoul(digits);
            } catch (...) {
              ok = false;
              return;
            }
            if (depth >= kMaxNestingDepthFn()) {
              ok = false;  // S1: hostile nesting → plain fallback.
              return;
            }
            const bool allowed = kind_allowed(digits, kind);
            i = open_end + 1;
            const size_t lo = text.size();
            parse(true, depth + 1);
            if (!ok) {
              return;
            }
            if (allowed) {
              tags.push_back(Tag{lo, text.size(), static_cast<uint32_t>(kind)});
            }
            // S2: out-of-allowlist tag skipped (text kept, no span).
          } else if (html.compare(i, 7, "</span>") == 0) {
            if (!nested) {
              ok = false;
              return;
            }
            i += 7;
            closed = true;
            return;
          } else if (html[i] == '<') {
            // Foreign markup (not our wrapper): not a payload we produced.
            ok = false;
            return;
          } else {
            const size_t next = html.find('<', i);
            const size_t end =
                next == std::string::npos ? html.size() : next;
            text += unescape(html.substr(i, end - i));
            i = end;
          }
        }
        if (nested && !closed) {
          ok = false;  // Unbalanced: missing </span>.
        }
      }
    };
    Parser parser{html, text, tags, ok, i};
    parser.parse(false);
    if (!ok || text.empty()) {
      buf.insert(pos, p.plain);
      return;
    }
    buf.insert(pos, text);
    for (const auto& t : tags) {
      if (t.lo < t.hi) {
        try {
          buf.tag_span(pos + t.lo, pos + t.hi, t.kind);
        } catch (...) {
        }
      }
    }
  } catch (...) {
  }
}

inline std::string degrade_to_plain(const ClipPayload& p) noexcept {
  try {
    return p.plain;
  } catch (...) {
    return std::string{};
  }
}
