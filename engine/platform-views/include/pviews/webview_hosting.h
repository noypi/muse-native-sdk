#pragma once

#include <string>

struct LayoutBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct WebSettings {
  bool safeBrowsingEnabled = true;
  bool allowFileAccess = false;
  std::string oauthRedirectUrl;
  bool customTabsHandoff = false;
};

struct HostResult {
  bool ok = false;
  std::string error;
};

struct JsResult {
  bool ok = false;
  std::string value;
  std::string error;
};

// Hosts a WebView through the Hybrid stub, enforcing engine bounds/clipping
// and policy gates (Safe Browsing on, file access off by default). OAuth
// redirect completes via the Custom Tabs handoff stub (no network).
// Out-of-bounds attach returns {false, "BOUNDS"}; policy violations return
// {false, "POLICY:<rule>"}. Never throws.
HostResult attachWebView(LayoutBounds b, WebSettings s) noexcept;

// Evaluates `script` via the T1 JS bridge allowlist gate. A registered
// script round-trips with {true, value, ""}; an unregistered script fails
// closed with {false, "", <non-empty error>}. Never throws.
JsResult evalJs(std::string script) noexcept;
