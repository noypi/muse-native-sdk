// sRGB-default color pipeline surface negotiation (FR-GRAPHICS-002).
// Pure logic, no Vulkan surface calls.
#include "engine/graphics/color/color_space.h"

NegotiatedSurface negotiateSurface(const SurfaceRequest& req,
                                    const DisplayCaps& caps) {
  NegotiatedSurface out;
  switch (req.preferred) {
    case Gamut::kHdr10:
      if (caps.hdr10) {
        out.gamut = Gamut::kHdr10;
        out.transfer = Transfer::kPq;
        out.fallback = false;
      } else {
        out.gamut = Gamut::kSrgb;
        out.transfer = Transfer::kSrgbGamma;
        out.fallback = true;
      }
      break;
    case Gamut::kP3:
      if (caps.p3) {
        out.gamut = Gamut::kP3;
        out.transfer = Transfer::kSrgbGamma;
        out.fallback = false;
      } else {
        out.gamut = Gamut::kSrgb;
        out.transfer = Transfer::kSrgbGamma;
        out.fallback = true;
      }
      break;
    case Gamut::kSrgb:
    default:
      out.gamut = Gamut::kSrgb;
      out.transfer = Transfer::kSrgbGamma;
      out.fallback = false;
      break;
  }
  return out;
}
