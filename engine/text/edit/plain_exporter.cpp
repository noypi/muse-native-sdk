// FR-TEXT-015 / AC-TEXT-043: rich→plain degradation implementation.
// ExportPlain returns the readable text untouched (span/image payloads live
// in separate members and are never consulted). ExportHtml escapes the text
// for HTML and appends one <img> tag per non-empty image payload.

#include "plain_exporter.h"

namespace muse::text::edit {
namespace {

std::string EscapeHtml(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
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
      default:
        out += c;
        break;
    }
  }
  return out;
}

}  // namespace

std::string ExportPlain(const RichSelection& sel) { return sel.text; }

std::string ExportHtml(const RichSelection& sel) {
  if (sel.text.empty() && sel.images.empty()) {
    return "";
  }
  std::string out = EscapeHtml(sel.text);
  if (!sel.images.empty()) {
    if (!out.empty()) {
      out += " ";
    }
    out += "<img alt=\"" + EscapeHtml(sel.images) + "\">";
  }
  return out;
}

}  // namespace muse::text::edit
