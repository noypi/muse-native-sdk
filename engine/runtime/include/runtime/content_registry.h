#pragma once

#include <map>
#include <string>
#include <string_view>

namespace runtime {

// Content tag identifying a registered content bundle (FR-RUNTIME-013).
// Tags are opaque strings resolved by exact match; dispatch keys on the tag
// alone and never on an app name or per-app token.
using ContentTag = std::string_view;

// Handler bundle installed for one content tag. The registry stores a copy
// per tag and hands out stable pointers to the stored copy; later batches
// fill in the dispatch surface behind this seam.
struct ContentHandlers {
  ContentHandlers() = default;
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

 private:
  // std::map (not unordered_map): node addresses stay stable across
  // inserts, so pointers handed out by Resolve never dangle after later
  // Register calls. Transparent comparator for string_view lookup.
  std::map<std::string, ContentHandlers, std::less<>> entries_;
};

}  // namespace runtime
