// T5 — fetch/budget/eviction glue (FR-MEDIA-004/AC-MEDIA-007 integration).
// Binds ONLY to the T5 `Interface:` line in plan.md:
//   `FetchResult fetchImageBytes(std::string url) :
//    FetchResult{bytes data; bool fromNet; Status status}`
// (`bytes` = `data`, `std::vector<uint8_t>`; `Status` shared with T1.)
//
// Hermetic: C++17 + `engine/media/include/media/fetch_bridge.h` only;
// test-local budget seam via `setFetchBudgetBytes` (0 forces kOverBudget
// refusal); no sockets, no network, no env/config reads, no filesystem,
// no live services. All fixtures are inline URL strings.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/media/include/media/fetch_bridge.h"

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

static constexpr std::size_t kAmpleBudget = 8u * 1024u * 1024u;

// --- cases -----------------------------------------------------------------

// Empty URL is a synchronous kInvalidArg reject that never throws and
// touches no net (no admission, no cache entry).
static void check_empty_url_rejected() {
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
  bool threw = false;
  media::FetchResult r{{}, false, media::Status::kOk};
  try {
    r = media::fetchImageBytes(std::string());
  } catch (...) {
    threw = true;
  }
  CHECK("empty url never throws", !threw);
  CHECK("empty url status kInvalidArg",
        r.status == media::Status::kInvalidArg);
  CHECK("empty url not fromNet", r.fromNet == false);
  CHECK("empty url no data", r.data.empty());
}

// Zero budget refuses with kOverBudget without fetching (empty data,
// never throws). A fresh URL is used so no cache hit can mask the refusal.
static void check_zero_budget_refused() {
  media::clearFetchCache();
  media::setFetchBudgetBytes(0);
  bool threw = false;
  media::FetchResult r{{1}, true, media::Status::kOk};
  try {
    r = media::fetchImageBytes("https://img.test/fetch-budget-0.png");
  } catch (...) {
    threw = true;
  }
  CHECK("over-budget never throws", !threw);
  CHECK("over-budget status kOverBudget",
        r.status == media::Status::kOverBudget);
  CHECK("over-budget not fromNet", r.fromNet == false);
  CHECK("over-budget no data", r.data.empty());
  media::setFetchBudgetBytes(kAmpleBudget);
}

// First fetch of a URL succeeds with kOk, non-empty data, fromNet=true.
static void check_first_fetch_from_net() {
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
  media::FetchResult r = media::fetchImageBytes("https://img.test/first.png");
  CHECK("first fetch status kOk", r.status == media::Status::kOk);
  CHECK("first fetch fromNet true", r.fromNet == true);
  CHECK("first fetch non-empty data", !r.data.empty());
}

// Repeat fetch of the same URL is a cache hit: kOk, fromNet=false, and
// the bytes match the first fetch exactly.
static void check_repeat_fetch_cache_hit() {
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
  const std::string url("https://img.test/repeat.png");
  media::FetchResult first = media::fetchImageBytes(url);
  media::FetchResult second = media::fetchImageBytes(url);
  CHECK("repeat first fetch kOk", first.status == media::Status::kOk);
  CHECK("repeat fetch status kOk", second.status == media::Status::kOk);
  CHECK("repeat fetch not fromNet", second.fromNet == false);
  CHECK("repeat fetch data matches first", second.data == first.data);
  CHECK("repeat fetch non-empty data", !second.data.empty());
}

// Security: hostile URLs (4KB garbage + embedded NUL) never throw and
// always yield a well-formed shape (kOk hit/miss or a clean rejection).
static void check_hostile_url_never_throws() {
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
  const std::string evil =
      std::string("https://img.test/") + std::string(4096, 'A') +
      std::string("\x00\xff\xfe../..", 8);
  bool threw = false;
  media::FetchResult r{{}, false, media::Status::kOk};
  try {
    r = media::fetchImageBytes(evil);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile url never throws", !threw);
  CHECK("hostile outcome is a valid shape",
        (r.status == media::Status::kOk) ||
            (r.status == media::Status::kInvalidArg) ||
            (r.status == media::Status::kOverBudget));
  if (r.status != media::Status::kOk) {
    CHECK("hostile reject carries no data", r.data.empty());
  } else {
    CHECK("hostile kOk carries data", !r.data.empty());
  }
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
}

int main() {
  check_empty_url_rejected();
  check_zero_budget_refused();
  check_first_fetch_from_net();
  check_repeat_fetch_cache_hit();
  check_hostile_url_never_throws();
  media::clearFetchCache();
  media::setFetchBudgetBytes(kAmpleBudget);
  if (s_failures == 0) {
    std::printf("ALL GREEN fetch_bridge_test\n");
    return 0;
  }
  std::printf("%d FAILURES fetch_bridge_test\n", s_failures);
  return 1;
}
