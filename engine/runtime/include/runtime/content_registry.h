#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

// Presented-scene tap entry (T8R6/F4; FR-INPUT-011 / AC-INPUT-020,
// FR-RUNTIME-013): plain-data seam so generic dispatch can rebuild the
// hit-test tree from the presented scene without naming any content
// bundle. Bounds are the scene's laid-out rects verbatim (never literals).
struct PresentedSceneTap {
  std::int32_t nodeId = -1;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

namespace runtime {

// Content tag identifying a registered content bundle (FR-RUNTIME-013).
// Tags are opaque strings resolved by exact match; dispatch keys on the tag
// alone and never on an app name or per-app token.
using ContentTag = std::string_view;

// Dispatch callbacks installed for one content tag. All pointers are
// optional (nullptr = this tag provides nothing on that path); dispatch
// treats a tag with no usable callback as unregistered for that path and
// fails closed per RULE-RUNTIME-001. Plain function pointers keep the
// header dependency-free; providers live in content modules, never in
// generic dispatch files.
using ContentTouchHandlerNameFn = const char* (*)(std::int32_t nodeId);
using ContentRecordTapFn = bool (*)(std::int32_t nodeId, const char* handler);
using ContentTapCountFn = int (*)(std::int32_t layoutRoot);
using ContentTapNodeIdFn = std::int32_t (*)(std::int32_t layoutRoot, int index);
using ContentTapHandlerFn = const char* (*)(std::int32_t layoutRoot,
                                            int index);
using ContentTapBoundsFn = bool (*)(std::int32_t layoutRoot,
                                    std::int32_t nodeId, float* x, float* y,
                                    float* w, float* h);
using ContentDrawKindFn = bool (*)(const std::string& kind,
                                   std::int32_t nodeId);

// Handler bundle installed for one content tag. The registry stores a copy
// per tag and hands out stable pointers to the stored copy; later batches
// fill in the dispatch surface behind this seam.
struct ContentHandlers {
  ContentHandlers() = default;
  ContentTouchHandlerNameFn touchHandlerName = nullptr;
  ContentRecordTapFn recordTap = nullptr;
  ContentTapCountFn tapCount = nullptr;
  ContentTapNodeIdFn tapNodeId = nullptr;
  ContentTapHandlerFn tapHandler = nullptr;
  ContentTapBoundsFn tapBounds = nullptr;
  ContentDrawKindFn drawSceneKind = nullptr;
};

// Tag-keyed content registry (FR-RUNTIME-013 / AC-RUNTIME-038/039).
// Fail-closed per RULE-RUNTIME-001: resolving an unknown or empty tag
// returns nullptr, never a default handler. Duplicate registration returns
// false and the first handlers win.
class ContentRegistry {
 public:
  ContentRegistry() = default;

  // Registers `handlers` under `tag`. Returns true on success, false when
  // the tag is empty or already registered (first wins).
  bool Register(ContentTag tag, const ContentHandlers& handlers);

  // Resolves the handlers for `tag`, or nullptr when unregistered/empty.
  const ContentHandlers* Resolve(ContentTag tag) const;

  // Visits every registration in tag order. Generic dispatch uses this to
  // resolve node-scoped queries (touch handler, tap record, scene-kind
  // draw) without naming a tag; the first registration answering the query
  // wins, otherwise the path fails closed.
  template <typename Fn>
  void ForEach(Fn&& fn) const {
    for (const auto& entry : entries_) {
      fn(static_cast<ContentTag>(entry.first), entry.second);
    }
  }
 private:
  // std::map (not unordered_map): node addresses stay stable across
  // inserts, so pointers handed out by Resolve never dangle after later
  // Register calls. Transparent comparator for string_view lookup.
  std::map<std::string, ContentHandlers, std::less<>> entries_;
};

// Presented-scene tap query (T8R6/F4; FR-RUNTIME-013 / AC-RUNTIME-039).
// Implemented by the content module; generic dispatch calls it without
// naming any content bundle. Fills up to `capacity` entries of the
// presented scene's laid-out touchables (bounds verbatim, never literals)
// and returns the count; returns 0 when the bytes are not a recognized
// content scene, when no touchable lays out, or on null/empty input —
// never negative, never throws. The caller keeps its fallback tree on 0.
int QueryPresentedSceneTaps(const void* data, std::size_t len,
                            ::PresentedSceneTap* out, int capacity);

// F3 divergence-probe diagnostic line (BUG-RUNTIME-014 / AC-RUNTIME-038).
// Implemented by the content module; generic dispatch logs the returned
// line verbatim without naming any content tag. Never throws.
std::string FormatContentDiagLine(const void* data, std::size_t len,
                                  bool presented,
                                  const char* contentErrOrNull,
                                  int fallbackRows);

}  // namespace runtime

// Process-wide registry consulted by generic dispatch. Content modules
// used to register their tags here; generic files resolve through it and
// never name content. Function-local static: safe initialization, no teardown
// ordering hazards (leaked on exit, never destroyed mid-dispatch).
runtime::ContentRegistry& GlobalContentRegistry();
