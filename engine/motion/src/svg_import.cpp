#include "motion/svg_import.h"

#include <cstddef>

namespace {

// Lowercase supported element names: paths, basic shapes, grouping, and
// transform/opacity keyframes (subset.md). The `svg` root container is
// neutral (allowed, not queried as a shape).
bool isSupportedElement(const std::string& element) {
  return element == "path" || element == "rect" || element == "circle" ||
         element == "ellipse" || element == "line" || element == "polyline" ||
         element == "polygon" || element == "g" || element == "animate" ||
         element == "animateTransform" || element == "set";
}

bool isAllowedContainer(const std::string& element) {
  return element == "svg";
}

// Extract the tag name from raw tag content (text between '<' and '>'):
// skips '?', '!', '/', and leading whitespace; the name ends at
// whitespace, '/', or '>'. Returns "" when there is no name (e.g. "<>").
std::string tagNameOf(const std::string& tag) {
  std::size_t i = 0;
  while (i < tag.size() &&
         (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\n' ||
          tag[i] == '\r' || tag[i] == '/' || tag[i] == '?' || tag[i] == '!')) {
    ++i;
  }
  // Skip comment/doctype declarations ("<!--", "<!DOCTYPE"): no element.
  if (!tag.empty() && tag[0] == '!') return "";
  if (i + 1 < tag.size() && tag[i] == '-' && tag[i + 1] == '-') return "";
  std::size_t start = i;
  while (i < tag.size() && tag[i] != ' ' && tag[i] != '\t' &&
         tag[i] != '\n' && tag[i] != '\r' && tag[i] != '/' && tag[i] != '>') {
    ++i;
  }
  if (start == i) return "";
  std::string name = tag.substr(start, i - start);
  // Strip a trailing '/' from self-closed spellings without space ("<g/>").
  while (!name.empty() && name.back() == '/') name.pop_back();
  // Drop XML namespace prefixes ("svg:path" -> "path") so prefixed
  // spellings never silently pass as supported; the bare local name is
  // what the subset table judges.
  std::size_t colon = name.rfind(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  return name;
}

// SMIL event-timing attributes are excluded from the subset even when the
// host element is supported (subset.md). Reports the attribute name.
//
// The scan is quote-aware and anchored to attribute-name position: quoted
// spans (single/double) are skipped so a literal such as
// `<path d="...onbegin=...">` inside an attribute value never matches, and a
// hit must start at an attribute boundary (preceded by ASCII whitespace or
// '/') with `name =` following. No over-approximation: only genuine
// attribute uses are reported.
std::string eventTimingAttrOf(const std::string& tag) {
  auto isSpace = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  };
  for (const char* attr : {"onbegin", "onend", "onrepeat"}) {
    std::string needle(attr);
    std::size_t pos = tag.find(needle);
    while (pos != std::string::npos) {
      // Determine whether `pos` lies inside a quoted span by re-scanning
      // from the tag start (tags are short; clarity over cleverness).
      bool inSingle = false;
      bool inDouble = false;
      for (std::size_t k = 0; k < pos; ++k) {
        if (inSingle) {
          if (tag[k] == '\'') inSingle = false;
        } else if (inDouble) {
          if (tag[k] == '"') inDouble = false;
        } else if (tag[k] == '\'') {
          inSingle = true;
        } else if (tag[k] == '"') {
          inDouble = true;
        }
      }
      bool quoted = inSingle || inDouble;
      // Attribute-name position: start of tag cannot be an attribute
      // (element name comes first); otherwise the preceding char must be
      // whitespace or '/' (self-close spacing).
      bool atBoundary =
          pos > 0 && (isSpace(tag[pos - 1]) || tag[pos - 1] == '/');
      if (!quoted && atBoundary) {
        std::size_t after = pos + needle.size();
        // Attribute use is `name=` possibly with whitespace before '='.
        std::size_t j = after;
        while (j < tag.size() && isSpace(tag[j])) ++j;
        if (j < tag.size() && tag[j] == '=') return needle;
      }
      pos = tag.find(needle, pos + 1);
    }
  }
  return "";
}

}  // namespace

SvgSupport motion_svgElementSupported(const std::string& element) {
  try {
    if (isSupportedElement(element)) return SvgSupport::kSupported;
    return SvgSupport::kUnsupported;
  } catch (...) {
    return SvgSupport::kUnsupported;
  }
}

ConversionResult motion_convertSvg(const std::vector<uint8_t>& svg) {
  try {
    if (svg.empty()) {
      return ConversionResult{AssetError::kUnsupported, "empty SVG input"};
    }
    // Oversized payloads are rejected without parsing so conversion never
    // throws, hangs, or allocates proportionally to hostile input.
    static const std::size_t kMaxSvgBytes = 512 * 1024;
    if (svg.size() > kMaxSvgBytes) {
      return ConversionResult{AssetError::kUnsupported,
                              "oversized SVG input (" +
                                  std::to_string(svg.size()) + " bytes)"};
    }
    std::string text(svg.begin(), svg.end());

    bool sawTag = false;
    bool sawSvgRoot = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
      std::size_t open = text.find('<', pos);
      if (open == std::string::npos) break;
      std::size_t close = text.find('>', open + 1);
      if (close == std::string::npos) {
        return ConversionResult{AssetError::kUnsupported,
                                "malformed SVG: unterminated tag"};
      }
      std::string tag = text.substr(open + 1, close - open - 1);
      pos = close + 1;

      std::string name = tagNameOf(tag);
      if (name.empty()) continue;  // comments, doctypes, PIs: ignored.
      // Closing tags carry no new constructs; the opener already judged.
      if (!tag.empty() && tag[0] == '/') continue;
      sawTag = true;
      if (name == "svg") {
        sawSvgRoot = true;
        continue;
      }
      // SMIL event timing is excluded even on supported hosts.
      std::string timed = eventTimingAttrOf(tag);
      if (!timed.empty()) {
        return ConversionResult{
            AssetError::kUnsupported,
            "unsupported SMIL event timing attribute: " + timed + " on <" +
                name + ">"};
      }
      if (isSupportedElement(name) || isAllowedContainer(name)) continue;
      // Out-of-subset element (filters, masks, script, foreignObject,
      // unknown names): fail loudly, naming the element. Never dropped.
      return ConversionResult{AssetError::kUnsupported,
                              "unsupported SVG element: " + name};
    }

    if (!sawTag || !sawSvgRoot) {
      return ConversionResult{AssetError::kUnsupported,
                              "unsupported SVG input: no <svg> root"};
    }
    return ConversionResult{AssetError::kOk, ""};
  } catch (...) {
    return ConversionResult{AssetError::kUnsupported,
                            "unsupported SVG input: conversion failed"};
  }
}
