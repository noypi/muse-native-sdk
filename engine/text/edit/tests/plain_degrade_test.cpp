// T7 — FR-TEXT-015 / AC-TEXT-043 (+AC-TEXT-044 part): rich→plain degradation
// on copy to plain-text targets (TC-TEXT-043).
// Hermetic harness: plain C++17, no test framework, no device, no clipboard.
// The services-owned clipboard boundary is MOCKED: tests assert only
// `ExportPlain` / `ExportHtml` outputs on an inline fake selection, never
// touching a real clipboard or provider.
// Shape mirrors engine/text/ink/tests/handwriting_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdio>
#include <string>

#include "../plain_exporter.h"

// Plan Interface lines (verbatim, plan.md Checklist T7):
// `std::string ExportPlain(const RichSelection& sel)` — drops spans/inline images, preserves readable text order; `std::string ExportHtml(const RichSelection& sel)` for rich targets (image → HTML).
//
// ASSUMPTION (structural, for the coder — see contract T7.md): the exporter
// lives in `muse::text::edit` as free functions with exactly the signatures
// above; `RichSelection` is a default-constructible plain struct with public
// `std::string text` (readable text in order), `std::string spans` (opaque
// span payload, dropped by ExportPlain), and `std::string images` (opaque
// inline-image payload, dropped by ExportPlain / rendered as `<img>` by
// ExportHtml). `MakeSel` below is test-local. The coder must not invent an
// alternate export API, and no clipboard/service header may be included.

namespace edit = muse::text::edit;

static edit::RichSelection MakeSel(const std::string& text,
                                   const std::string& spans,
                                   const std::string& images) {
  edit::RichSelection sel{};
  sel.text = text;
  sel.spans = spans;
  sel.images = images;
  return sel;
}

static bool check_plain_drops_spans_keeps_order() {
  edit::RichSelection sel = MakeSel("hello world", "bold[0,5)italic[6,11)", "");
  return edit::ExportPlain(sel) == "hello world";
}

static bool check_plain_drops_inline_image() {
  edit::RichSelection sel = MakeSel("see this", "", "img:1");
  return edit::ExportPlain(sel) == "see this";
}

static bool check_plain_mixed_spans_and_image() {
  edit::RichSelection sel =
      MakeSel("caption here", "bold[0,7)", "img:9");
  const std::string out = edit::ExportPlain(sel);
  return out == "caption here" && out.find('<') == std::string::npos;
}

static bool check_plain_never_contains_markup() {
  // Security: span payloads may carry attacker-shaped strings; plain output
  // must be bare text with no tags or entities derived from spans/images.
  edit::RichSelection sel =
      MakeSel("click me", "<script>alert(1)</script>", "<img src=x onerror=y>");
  const std::string out = edit::ExportPlain(sel);
  return out == "click me";
}

static bool check_plain_empty_selection_is_empty() {
  edit::RichSelection sel{};
  return edit::ExportPlain(sel) == "";
}

static bool check_plain_multibyte_order_preserved() {
  const std::string text = u8"a\u4e2d\u6587b\U0001F600c";
  edit::RichSelection sel = MakeSel(text, "bold[0,9)", "img:2");
  return edit::ExportPlain(sel) == text;
}

static bool check_html_keeps_text_and_image() {
  edit::RichSelection sel = MakeSel("see this", "bold[0,3)", "img:1");
  const std::string out = edit::ExportHtml(sel);
  return out.find("see this") != std::string::npos &&
         out.find("<img") != std::string::npos;
}

static bool check_html_without_image_has_no_img_tag() {
  edit::RichSelection sel = MakeSel("plain words", "italic[0,5)", "");
  const std::string out = edit::ExportHtml(sel);
  return out.find("plain words") != std::string::npos &&
         out.find("<img") == std::string::npos;
}

static bool check_html_empty_selection_is_empty() {
  edit::RichSelection sel{};
  return edit::ExportHtml(sel) == "";
}

static bool check_large_selection_exports_plain() {
  // Boundary: 64 KiB text with span/image payloads exports exactly.
  const std::string text(65536, 'z');
  edit::RichSelection sel = MakeSel(text, std::string(4096, 's'), "img:big");
  return edit::ExportPlain(sel) == text;
}

static bool check_signature_shapes_match_plan_interface() {
  edit::RichSelection sel = MakeSel("t", "", "");
  const std::string p = edit::ExportPlain(sel);
  const std::string h = edit::ExportHtml(sel);
  return p == "t" && h.find("t") != std::string::npos;
}

int main() {
  static struct {
    const char* name;
    bool (*fn)();
  } cases[] = {
      {"check_plain_drops_spans_keeps_order", check_plain_drops_spans_keeps_order},
      {"check_plain_drops_inline_image", check_plain_drops_inline_image},
      {"check_plain_mixed_spans_and_image", check_plain_mixed_spans_and_image},
      {"check_plain_never_contains_markup", check_plain_never_contains_markup},
      {"check_plain_empty_selection_is_empty", check_plain_empty_selection_is_empty},
      {"check_plain_multibyte_order_preserved", check_plain_multibyte_order_preserved},
      {"check_html_keeps_text_and_image", check_html_keeps_text_and_image},
      {"check_html_without_image_has_no_img_tag",
       check_html_without_image_has_no_img_tag},
      {"check_html_empty_selection_is_empty", check_html_empty_selection_is_empty},
      {"check_large_selection_exports_plain", check_large_selection_exports_plain},
      {"check_signature_shapes_match_plan_interface",
       check_signature_shapes_match_plan_interface},
  };
  int failures = 0;
  for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    bool ok = false;
    try {
      ok = cases[i].fn();
    } catch (...) {
      ok = false;
    }
    if (!ok) {
      ++failures;
      std::printf("FAIL %s\n", cases[i].name);
    } else {
      std::printf("PASS %s\n", cases[i].name);
    }
  }
  return failures == 0 ? 0 : 1;
}
