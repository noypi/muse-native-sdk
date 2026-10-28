#pragma once

// FR-TEXT-015 / AC-TEXT-043 (+AC-TEXT-044 part): rich→plain degradation on
// copy to plain-text targets (TC-TEXT-043).
// Free-function exporter over an inline selection value: plain targets get
// bare readable text (spans and inline images dropped), rich targets get
// HTML text with images kept as <img>. The services-owned clipboard boundary
// is out of scope here — callers pass the selection value only, no live
// clipboard is touched. Never throws (bad_alloc aside).

#include <string>

namespace muse::text::edit {

// Rich selection value: `text` is the readable text in order, `spans` is an
// opaque span payload (dropped by ExportPlain), `images` is an opaque
// inline-image payload (dropped by ExportPlain, rendered as `<img>` by
// ExportHtml). Default-constructs to the empty selection.
struct RichSelection {
  std::string text;
  std::string spans;
  std::string images;
};

// Returns the readable text in order, dropping span and inline-image
// payloads. Span/image strings are never consulted, so attacker-shaped
// payloads cannot leak markup or entities into the output.
std::string ExportPlain(const RichSelection& sel);

// Returns HTML for rich targets: the readable text (HTML-escaped) with each
// inline-image payload rendered as an `<img>` tag. Empty selection yields
// the empty string; a selection without images contains no `<img>` tag.
std::string ExportHtml(const RichSelection& sel);

}  // namespace muse::text::edit
