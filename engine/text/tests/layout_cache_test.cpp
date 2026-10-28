// TC-TEXT-012 — AC-TEXT-012: static layout cacheable/reusable across frames
// when text, style, width, scale unchanged.
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. Cases drive only the published layout-cache interface with
// small inline inputs; no implementation internals are asserted.
// Shape mirrors engine/text/tests/jni_free_path_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/layout_cache.h"
#include "muse/text/text_types.h"

// ASSUMPTION: muse::text::LayoutCache is a default-constructible class in
// muse/text/layout_cache.h exposing:
//   std::shared_ptr<const StaticLayout> layoutStatic(const LayoutKey& key);
//   void invalidate(uint64_t fontSetGen);
// where LayoutKey is exactly:
//   struct LayoutKey { std::string textUtf8; TextStyle style; float widthPx;
//                      float scale; uint64_t fontSetGen; };
// Only pointer identity / inequality is asserted; no StaticLayout member is
// touched, so no assumption about StaticLayout's shape is needed.

static muse::text::LayoutKey makeKey(const std::string& text, float widthPx,
                                     float scale, uint64_t gen) {
  muse::text::LayoutKey key;
  key.textUtf8 = text;
  key.style = muse::text::TextStyle{};
  key.widthPx = widthPx;
  key.scale = scale;
  key.fontSetGen = gen;
  return key;
}

static bool check_identical_key_twice_returns_pointer_identical() {
  muse::text::LayoutCache cache;
  const muse::text::LayoutKey key = makeKey("Hello", 200.0f, 1.0f, 7u);
  std::shared_ptr<const muse::text::StaticLayout> first =
      cache.layoutStatic(key);
  std::shared_ptr<const muse::text::StaticLayout> second =
      cache.layoutStatic(key);
  if (first.get() == nullptr || second.get() == nullptr) return false;
  return first.get() == second.get();
}

static bool check_width_change_is_miss() {
  muse::text::LayoutCache cache;
  std::shared_ptr<const muse::text::StaticLayout> wide =
      cache.layoutStatic(makeKey("Hello", 200.0f, 1.0f, 7u));
  std::shared_ptr<const muse::text::StaticLayout> narrow =
      cache.layoutStatic(makeKey("Hello", 100.0f, 1.0f, 7u));
  if (wide.get() == nullptr || narrow.get() == nullptr) return false;
  return wide.get() != narrow.get();
}

static bool check_scale_change_is_miss() {
  muse::text::LayoutCache cache;
  std::shared_ptr<const muse::text::StaticLayout> a =
      cache.layoutStatic(makeKey("Hello", 200.0f, 1.0f, 7u));
  std::shared_ptr<const muse::text::StaticLayout> b =
      cache.layoutStatic(makeKey("Hello", 200.0f, 2.0f, 7u));
  if (a.get() == nullptr || b.get() == nullptr) return false;
  return a.get() != b.get();
}

static bool check_different_text_is_miss() {
  muse::text::LayoutCache cache;
  std::shared_ptr<const muse::text::StaticLayout> a =
      cache.layoutStatic(makeKey("Hello", 200.0f, 1.0f, 7u));
  std::shared_ptr<const muse::text::StaticLayout> b =
      cache.layoutStatic(makeKey("World", 200.0f, 1.0f, 7u));
  if (a.get() == nullptr || b.get() == nullptr) return false;
  return a.get() != b.get();
}

static bool check_invalidate_gen_drops_entries() {
  muse::text::LayoutCache cache;
  const muse::text::LayoutKey key = makeKey("Hello", 200.0f, 1.0f, 7u);
  std::shared_ptr<const muse::text::StaticLayout> before =
      cache.layoutStatic(key);
  cache.invalidate(7u);
  std::shared_ptr<const muse::text::StaticLayout> after =
      cache.layoutStatic(key);
  if (before.get() == nullptr || after.get() == nullptr) return false;
  return before.get() != after.get();
}

static bool check_invalidate_other_gen_keeps_entry() {
  muse::text::LayoutCache cache;
  const muse::text::LayoutKey key = makeKey("Hello", 200.0f, 1.0f, 7u);
  std::shared_ptr<const muse::text::StaticLayout> before =
      cache.layoutStatic(key);
  cache.invalidate(8u);
  std::shared_ptr<const muse::text::StaticLayout> after =
      cache.layoutStatic(key);
  if (before.get() == nullptr || after.get() == nullptr) return false;
  return before.get() == after.get();
}

static bool check_empty_text_caches() {
  muse::text::LayoutCache cache;
  const muse::text::LayoutKey key = makeKey("", 200.0f, 1.0f, 7u);
  std::shared_ptr<const muse::text::StaticLayout> first =
      cache.layoutStatic(key);
  std::shared_ptr<const muse::text::StaticLayout> second =
      cache.layoutStatic(key);
  if (first.get() == nullptr || second.get() == nullptr) return false;
  return first.get() == second.get();
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_identical_key_twice_returns_pointer_identical);
  RUN_CASE(check_width_change_is_miss);
  RUN_CASE(check_scale_change_is_miss);
  RUN_CASE(check_different_text_is_miss);
  RUN_CASE(check_invalidate_gen_drops_entries);
  RUN_CASE(check_invalidate_other_gen_keeps_entry);
  RUN_CASE(check_empty_text_caches);
  if (failures > 0) {
    std::printf("layout_cache_test: %d failing\n", failures);
    return 1;
  }
  std::printf("layout_cache_test: all passing\n");
  return 0;
}
