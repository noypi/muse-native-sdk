// T3 contract tests — FR-PVIEWS-011 / AC-PVIEWS-018.
// Binds ONLY to the plan T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/webview_hosting.h only; stub Hybrid host (no device,
// no activity/window), fake clip/scroll values, no network, no env/config reads,
// no filesystem, no live services, no real credentials/tokens, no OAuth network.
// ASSUMPTION (see unit-tests/T3.md): the header additionally declares
//   struct LayoutBounds { int left, top, right, bottom; };
//   struct WebSettings { bool safeBrowsingEnabled = true;
//                        bool allowFileAccess = false;
//                        std::string oauthRedirectUrl;
//                        bool customTabsHandoff = false; };
//   struct HostResult { bool ok; std::string error; };
//   JsResult is `{ bool ok; std::string value; std::string error; }` per the
//   Interface line. Engine bounds are enforced internally by attachWebView;
//   inverted/empty/negative rects are out-of-bounds. OAuth redirect completes
//   via Custom Tabs handoff stub with no network when settings carry a dummy
//   https redirect URL. evalJs routes through the T1 bridge internally, so an
//   unregistered script fails closed with ok:false (no throw).
#include <cstdio>
#include <string>

#include "pviews/webview_hosting.h"

namespace {

LayoutBounds insideBounds() {
  LayoutBounds b{};
  b.left = 0;
  b.top = 0;
  b.right = 200;
  b.bottom = 400;
  return b;
}

WebSettings defaultSettings() {
  WebSettings s{};
  s.safeBrowsingEnabled = true;
  s.allowFileAccess = false;
  return s;
}

// Case: in-bounds attach with defaults hosts successfully.
bool check_attach_inside_bounds_ok() {
  const HostResult r = attachWebView(insideBounds(), defaultSettings());
  return r.ok && r.error.empty();
}

// Case: out-of-bounds attach (inverted rect) fails with exact BOUNDS shape.
bool check_attach_out_of_bounds_shape() {
  LayoutBounds b{};
  b.left = 300;
  b.top = 0;
  b.right = 100;  // right < left -> outside engine bounds
  b.bottom = 400;
  const HostResult r = attachWebView(b, defaultSettings());
  return !r.ok && r.error == "BOUNDS";
}

// Case: negative-origin rect is out-of-bounds, never throws.
bool check_attach_negative_origin_bounds() {
  LayoutBounds b{};
  b.left = -50;
  b.top = -50;
  b.right = 100;
  b.bottom = 100;
  bool threw = false;
  HostResult r{};
  try {
    r = attachWebView(b, defaultSettings());
  } catch (...) {
    threw = true;
  }
  return !threw && !r.ok && r.error == "BOUNDS";
}

// Case: file access enabled violates default policy -> POLICY:<rule>.
bool check_file_access_policy_shape() {
  WebSettings s = defaultSettings();
  s.allowFileAccess = true;
  const HostResult r = attachWebView(insideBounds(), s);
  return !r.ok && r.error.rfind("POLICY:", 0) == 0;
}

// Case: Safe Browsing disabled violates policy -> POLICY:<rule>.
bool check_safe_browsing_policy_shape() {
  WebSettings s = defaultSettings();
  s.safeBrowsingEnabled = false;
  const HostResult r = attachWebView(insideBounds(), s);
  return !r.ok && r.error.rfind("POLICY:", 0) == 0;
}

// Case: evalJs round-trips a registered script after a good attach.
bool check_eval_js_round_trip() {
  const HostResult h = attachWebView(insideBounds(), defaultSettings());
  if (!h.ok) return false;
  const JsResult r = evalJs("bridge.echo('hello')");
  return r.ok && r.error.empty() && !r.value.empty();
}

// Case: evalJs of an unregistered script fails closed (ok:false), never throws.
bool check_eval_js_unregistered_fails_closed() {
  const HostResult h = attachWebView(insideBounds(), defaultSettings());
  if (!h.ok) return false;
  bool threw = false;
  JsResult r{};
  try {
    r = evalJs("bridge.notRegistered('hello')");
  } catch (...) {
    threw = true;
  }
  return !threw && !r.ok && !r.error.empty();
}

// Case: OAuth redirect completes via Custom Tabs handoff stub (no network).
bool check_oauth_custom_tabs_handoff() {
  WebSettings s = defaultSettings();
  s.oauthRedirectUrl = "https://app.test.invalid/oauth/callback";
  s.customTabsHandoff = true;
  const HostResult r = attachWebView(insideBounds(), s);
  return r.ok && r.error.empty();
}

// Case: scroll-offset re-attach keeps clip/hosting, never throws.
bool check_scroll_reattach_keeps_hosting() {
  const HostResult first = attachWebView(insideBounds(), defaultSettings());
  if (!first.ok) return false;
  LayoutBounds scrolled = insideBounds();
  scrolled.top += 100;
  scrolled.bottom += 100;
  bool threw = false;
  HostResult r{};
  try {
    r = attachWebView(scrolled, defaultSettings());
  } catch (...) {
    threw = true;
  }
  return !threw && r.ok;
}

// Case: attachWebView/evalJs never throw on hostile/oversized input.
bool check_never_throws() {
  try {
    LayoutBounds empty{};
    attachWebView(empty, defaultSettings());
    WebSettings s = defaultSettings();
    attachWebView(insideBounds(), s);
    evalJs("");
    evalJs(std::string(1 << 16, 'x'));
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
  RUN_CASE(check_attach_inside_bounds_ok);
  RUN_CASE(check_attach_out_of_bounds_shape);
  RUN_CASE(check_attach_negative_origin_bounds);
  RUN_CASE(check_file_access_policy_shape);
  RUN_CASE(check_safe_browsing_policy_shape);
  RUN_CASE(check_eval_js_round_trip);
  RUN_CASE(check_eval_js_unregistered_fails_closed);
  RUN_CASE(check_oauth_custom_tabs_handoff);
  RUN_CASE(check_scroll_reattach_keeps_hosting);
  RUN_CASE(check_never_throws);
  if (failures > 0) {
    std::printf("webview_hosting_test: %d failing\n", failures);
    return 1;
  }
  std::printf("webview_hosting_test: all passing\n");
  return 0;
}
