// T3 hermetic unit test — color-space surface negotiation
// (FR-GRAPHICS-002, AC-GRAPHICS-003).
// Style: plain asserts, no dependencies, no I/O, no network, no Vulkan calls.
#include <cassert>
#include <cstdio>

#include "engine/graphics/color/color_space.h"

int main() {
  // 1. Default request negotiates sRGB with gamma-correct blend, no fallback.
  {
    SurfaceRequest req;
    DisplayCaps caps;
    NegotiatedSurface s = negotiateSurface(req, caps);
    assert(s.gamut == Gamut::kSrgb);
    assert(s.transfer == Transfer::kSrgbGamma);
    assert(s.fallback == false);
    printf("PASS default-negotiates-srgb\n");
  }
  // 2. HDR10 request on SDR caps falls back to sRGB with fallback=true.
  {
    SurfaceRequest req;
    req.preferred = Gamut::kHdr10;
    DisplayCaps caps;  // SDR: no hdr10, no p3.
    NegotiatedSurface s = negotiateSurface(req, caps);
    assert(s.gamut == Gamut::kSrgb);
    assert(s.transfer == Transfer::kSrgbGamma);
    assert(s.fallback == true);
    printf("PASS hdr10-on-sdr-falls-back-srgb\n");
  }
  // 3. P3 request on P3 caps returns P3 with fallback=false.
  {
    SurfaceRequest req;
    req.preferred = Gamut::kP3;
    DisplayCaps caps;
    caps.p3 = true;
    NegotiatedSurface s = negotiateSurface(req, caps);
    assert(s.gamut == Gamut::kP3);
    assert(s.fallback == false);
    printf("PASS p3-on-p3-caps-no-fallback\n");
  }
  // 4. HDR10 request on HDR10 caps selects HDR10, no fallback.
  {
    SurfaceRequest req;
    req.preferred = Gamut::kHdr10;
    DisplayCaps caps;
    caps.hdr10 = true;
    caps.p3 = true;
    NegotiatedSurface s = negotiateSurface(req, caps);
    assert(s.gamut == Gamut::kHdr10);
    assert(s.fallback == false);
    printf("PASS hdr10-on-hdr-caps-selected\n");
  }
  // 5. Boundary: P3 request on SDR caps falls back to sRGB with fallback=true.
  // ASSUMPTION: unsupported preferred gamut always degrades to sRGB, never
  // errors, mirroring the HDR10-fallback rule.
  {
    SurfaceRequest req;
    req.preferred = Gamut::kP3;
    DisplayCaps caps;  // SDR.
    NegotiatedSurface s = negotiateSurface(req, caps);
    assert(s.gamut == Gamut::kSrgb);
    assert(s.fallback == true);
    printf("PASS p3-on-sdr-falls-back-srgb\n");
  }
  // 6. Fallback path never errors: transfer stays sRGB gamma on fallback.
  {
    SurfaceRequest req;
    req.preferred = Gamut::kHdr10;
    DisplayCaps caps;
    caps.p3 = true;  // P3 capable but not HDR10.
    NegotiatedSurface s = negotiateSurface(req, caps);
    // ASSUMPTION: HDR10-on-P3-only-caps falls back to sRGB (HDR10 requires the
    // caps flag); it does not silently downgrade to P3.
    assert(s.gamut == Gamut::kSrgb);
    assert(s.transfer == Transfer::kSrgbGamma);
    assert(s.fallback == true);
    printf("PASS hdr10-on-p3-only-falls-back-srgb\n");
  }
  printf("ALL T3 TESTS PASSED\n");
  return 0;
}
