#pragma once

#include <string>
#include <vector>

struct CameraState {
  double lat = 0.0;
  double lng = 0.0;
  double zoom = 0.0;
  double tilt = 0.0;
  double bearing = 0.0;
};

struct Marker {
  std::string id;
  double lat = 0.0;
  double lng = 0.0;
};

struct TouchEvent {
  double x = 0.0;
  double y = 0.0;
};

struct MapResult {
  bool ok = false;
  CameraState state;
  std::string error;
};

struct TouchResult {
  bool handled = false;
};

// Selects the fake map provider behind the Hybrid stub: "google" |
// "maplibre". Anything else marks the provider unknown; the next
// applyCamera/setMarkers then fails closed with {false, "PROVIDER"}.
// Never throws.
void setMapProvider(const std::string& name) noexcept;

// Simulates a config-change recreate: re-embeds the current provider while
// preserving the last applied camera state. Never throws.
void recreateMap() noexcept;

// Applies a camera update to the embedded provider. Both fake providers
// (google/maplibre) store and echo the camera with {true, state, ""};
// an unknown provider returns {false, state, "PROVIDER"}. Never throws.
MapResult applyCamera(CameraState c) noexcept;

// Bridges a marker list onto the map surface, preserving the camera.
// An empty list clears markers and still reports ok. Unknown provider
// returns {false, state, "PROVIDER"}. Never throws.
MapResult setMarkers(std::vector<Marker> m) noexcept;

// Routes a touch through the spatial-hash stub: on-surface points return
// {true}, off-surface points return {false}. Never throws, never an
// exception — off-surface is an unhandled result, not an error.
TouchResult routeTouch(TouchEvent e) noexcept;
