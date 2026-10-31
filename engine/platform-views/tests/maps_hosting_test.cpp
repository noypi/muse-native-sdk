// T4 contract tests — FR-PVIEWS-012 / AC-PVIEWS-019.
// Binds ONLY to the plan T4 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/maps_hosting.h only; fake Google + MapLibre
// providers behind one provider interface riding a Hybrid stub (no device, no
// SDK download, no API keys), spatial-hash gesture routing stub, no network,
// no env/config reads, no filesystem, no live services (GR-05 R5.3).
// ASSUMPTION (see unit-tests/T4.md): the header additionally declares
//   void setMapProvider(const std::string& name);  // "google" | "maplibre"
//   void recreateMap();  // config-change recreate preserving camera state
//   struct CameraState { double lat, lng, zoom, tilt, bearing; };
//   struct Marker { std::string id; double lat, lng; };
//   struct TouchEvent { double x, y; };
//   MapResult is `{ bool ok; CameraState state; std::string error; }` per the
//   Interface line; TouchResult is `{ bool handled; }` where off-surface
//   returns {false} and never throws.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "pviews/maps_hosting.h"

namespace {

bool sameCamera(const CameraState& a, const CameraState& b) {
  return a.lat == b.lat && a.lng == b.lng && a.zoom == b.zoom &&
         a.tilt == b.tilt && a.bearing == b.bearing;
}

CameraState homeCamera() {
  CameraState c{};
  c.lat = 12.5;
  c.lng = 77.5;
  c.zoom = 14.0;
  c.tilt = 0.0;
  c.bearing = 0.0;
  return c;
}

std::vector<Marker> twoMarkers() {
  Marker a{};
  a.id = "m1";
  a.lat = 12.5;
  a.lng = 77.5;
  Marker b{};
  b.id = "m2";
  b.lat = 12.6;
  b.lng = 77.6;
  return {a, b};
}

// Case: Google provider embeds and applies a camera.
bool check_google_apply_camera() {
  setMapProvider("google");
  const MapResult r = applyCamera(homeCamera());
  return r.ok && r.error.empty() && sameCamera(r.state, homeCamera());
}

// Case: MapLibre provider embeds and applies a camera.
bool check_maplibre_apply_camera() {
  setMapProvider("maplibre");
  const MapResult r = applyCamera(homeCamera());
  return r.ok && r.error.empty() && sameCamera(r.state, homeCamera());
}

// Case: unknown provider fails with exact PROVIDER shape, never throws.
bool check_unknown_provider_shape() {
  setMapProvider("acme-maps-not-real");
  bool threw = false;
  MapResult r{};
  try {
    r = applyCamera(homeCamera());
  } catch (...) {
    threw = true;
  }
  return !threw && !r.ok && r.error == "PROVIDER";
}

// Case: camera state survives a config-change recreate round-trip.
bool check_camera_survives_recreate() {
  setMapProvider("google");
  CameraState c = homeCamera();
  c.zoom = 16.0;
  c.tilt = 30.0;
  const MapResult first = applyCamera(c);
  if (!first.ok) return false;
  recreateMap();
  const MapResult second = applyCamera(first.state);
  return second.ok && sameCamera(second.state, c);
}

// Case: markers set on the surface and report ok with camera preserved.
bool check_set_markers_ok() {
  setMapProvider("google");
  const MapResult cam = applyCamera(homeCamera());
  if (!cam.ok) return false;
  const MapResult r = setMarkers(twoMarkers());
  return r.ok && r.error.empty() && sameCamera(r.state, homeCamera());
}

// Case: empty marker list clears and still reports ok.
bool check_set_markers_empty_ok() {
  setMapProvider("maplibre");
  if (!applyCamera(homeCamera()).ok) return false;
  const MapResult r = setMarkers({});
  return r.ok && r.error.empty();
}

// Case: on-surface touch routes handled, never throws.
bool check_on_surface_touch_handled() {
  setMapProvider("google");
  if (!applyCamera(homeCamera()).ok) return false;
  TouchEvent e{};
  e.x = 100.0;
  e.y = 200.0;
  bool threw = false;
  TouchResult t{};
  try {
    t = routeTouch(e);
  } catch (...) {
    threw = true;
  }
  return !threw && t.handled;
}

// Case: off-surface touch returns unhandled, never an exception.
bool check_off_surface_touch_unhandled() {
  setMapProvider("google");
  if (!applyCamera(homeCamera()).ok) return false;
  TouchEvent e{};
  e.x = -100000.0;
  e.y = -100000.0;
  bool threw = false;
  TouchResult t{};
  try {
    t = routeTouch(e);
  } catch (...) {
    threw = true;
  }
  return !threw && !t.handled;
}

// Case: pan/zoom/tilt camera updates route correctly per provider.
bool check_pan_zoom_tilt_route() {
  setMapProvider("maplibre");
  CameraState pan = homeCamera();
  pan.lat += 0.1;
  pan.lng += 0.1;
  const MapResult r1 = applyCamera(pan);
  CameraState zoom = pan;
  zoom.zoom = 18.0;
  const MapResult r2 = applyCamera(zoom);
  CameraState tilt = zoom;
  tilt.tilt = 45.0;
  const MapResult r3 = applyCamera(tilt);
  return r1.ok && sameCamera(r1.state, pan) && r2.ok &&
         sameCamera(r2.state, zoom) && r3.ok && sameCamera(r3.state, tilt);
}

// Case: API never throws on hostile/oversized input.
bool check_never_throws() {
  try {
    setMapProvider("google");
    CameraState c{};
    c.lat = std::numeric_limits<double>::quiet_NaN();
    applyCamera(c);
    std::vector<Marker> many(1000);
    for (std::size_t i = 0; i < many.size(); ++i) {
      many[i].id = "m" + std::to_string(i);
    }
    setMarkers(many);
    TouchEvent e{};
    routeTouch(e);
    setMapProvider("");
    applyCamera(homeCamera());
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_google_apply_camera);
  RUN_CASE(check_maplibre_apply_camera);
  RUN_CASE(check_unknown_provider_shape);
  RUN_CASE(check_camera_survives_recreate);
  RUN_CASE(check_set_markers_ok);
  RUN_CASE(check_set_markers_empty_ok);
  RUN_CASE(check_on_surface_touch_handled);
  RUN_CASE(check_off_surface_touch_unhandled);
  RUN_CASE(check_pan_zoom_tilt_route);
  RUN_CASE(check_never_throws);
  if (failures > 0) {
    std::printf("maps_hosting_test: %d failing\n", failures);
    return 1;
  }
  std::printf("maps_hosting_test: all passing\n");
  return 0;
}
