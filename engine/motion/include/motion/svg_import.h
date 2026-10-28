#ifndef MOTION_SVG_IMPORT_H_
#define MOTION_SVG_IMPORT_H_

#include <string>
#include <vector>

#include "motion/asset.h"

// Build-time SVG subset import (FR-MOTION-010 / AC-MOTION-030).
//
// Declared supported subset (see engine/motion/svg/subset.md):
//   paths, basic shapes, grouping, and transform/opacity keyframes —
//   element names (lowercase): path, rect, circle, ellipse, line,
//   polyline, polygon, g, animate, animateTransform, set,
//   plus the `svg` root container.
// Everything else (filters, masks, script, foreignObject, SMIL event
// timing, unknown elements) is out of subset: the query reports
// kUnsupported and conversion fails loudly with a reason naming the
// element — never a silent drop. Empty input is kUnsupported with a
// non-empty reason; oversized input returns a valid AssetError and
// never throws.

enum class SvgSupport { kSupported, kUnsupported };

struct ConversionResult {
  AssetError error;
  std::string unsupportedReason;
};

// Reports whether an SVG element name is in the supported subset.
// Lowercase literal names only; empty and unknown names are kUnsupported.
// Never throws.
SvgSupport motion_svgElementSupported(const std::string& element);

// Converts an in-subset SVG payload. kOk with an empty reason when every
// element is in subset; kUnsupported with a reason naming the offending
// element otherwise (including empty input). Never throws.
ConversionResult motion_convertSvg(const std::vector<uint8_t>& svg);

#endif  // MOTION_SVG_IMPORT_H_
