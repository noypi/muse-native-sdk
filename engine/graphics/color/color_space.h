// sRGB-default color pipeline surface negotiation (FR-GRAPHICS-002).
// Pure logic, no Vulkan surface calls.
#ifndef ENGINE_GRAPHICS_COLOR_COLOR_SPACE_H_
#define ENGINE_GRAPHICS_COLOR_COLOR_SPACE_H_

enum class Gamut { kSrgb = 0, kP3 = 1, kHdr10 = 2 };

enum class Transfer { kSrgbGamma = 0, kPq = 1 };

struct SurfaceRequest {
  Gamut preferred = Gamut::kSrgb;
};

struct DisplayCaps {
  bool hdr10 = false;
  bool p3 = false;
};

struct NegotiatedSurface {
  Gamut gamut = Gamut::kSrgb;
  Transfer transfer = Transfer::kSrgbGamma;
  bool fallback = false;
};

// Negotiates the surface gamut/transfer for a request against display caps.
// Never errors: unsupported preferences fall back to sRGB with fallback=true.
NegotiatedSurface negotiateSurface(const SurfaceRequest& req,
                                    const DisplayCaps& caps);

#endif  // ENGINE_GRAPHICS_COLOR_COLOR_SPACE_H_
