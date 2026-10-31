#include "pviews/maps_hosting.h"

namespace {

// Fake providers behind the single Hybrid stub. Anything else is unknown
// and fails closed with "PROVIDER".
std::string g_provider;
CameraState g_camera;
std::vector<Marker> g_markers;

// Stub map surface extent. Touches inside route to the map; far-negative
// (or otherwise out-of-extent) coordinates are off-surface.
constexpr double kSurfaceW = 4096.0;
constexpr double kSurfaceH = 4096.0;

bool knownProvider(const std::string& p) noexcept {
  return p == "google" || p == "maplibre";
}

bool onSurface(double x, double y) noexcept {
  if (!(x >= 0.0) || !(y >= 0.0)) return false;
  if (!(x < kSurfaceW) || !(y < kSurfaceH)) return false;
  return true;
}

}  // namespace

void setMapProvider(const std::string& name) noexcept {
  try {
    g_provider = name;
  } catch (...) {
    // Keep the previous provider on allocation failure; never throws.
  }
}

void recreateMap() noexcept {
  try {
    // Re-embed under the current provider, preserving camera and markers.
    // Stub work only: state already lives in g_camera/g_markers.
    if (!knownProvider(g_provider)) return;
  } catch (...) {
  }
}

MapResult applyCamera(CameraState c) noexcept {
  try {
    MapResult r;
    if (!knownProvider(g_provider)) {
      r.ok = false;
      r.state = c;
      r.error = "PROVIDER";
      return r;
    }
    g_camera = c;
    r.ok = true;
    r.state = g_camera;
    r.error.clear();
    return r;
  } catch (...) {
    try {
      MapResult r;
      r.ok = false;
      r.error = "PROVIDER";
      return r;
    } catch (...) {
      MapResult r;
      return r;
    }
  }
}

MapResult setMarkers(std::vector<Marker> m) noexcept {
  try {
    MapResult r;
    if (!knownProvider(g_provider)) {
      r.ok = false;
      r.state = g_camera;
      r.error = "PROVIDER";
      return r;
    }
    g_markers = m;
    r.ok = true;
    r.state = g_camera;
    r.error.clear();
    return r;
  } catch (...) {
    try {
      MapResult r;
      r.ok = false;
      r.error = "PROVIDER";
      return r;
    } catch (...) {
      MapResult r;
      return r;
    }
  }
}

TouchResult routeTouch(TouchEvent e) noexcept {
  try {
    TouchResult t;
    t.handled = onSurface(e.x, e.y);
    return t;
  } catch (...) {
    TouchResult t;
    t.handled = false;
    return t;
  }
}
