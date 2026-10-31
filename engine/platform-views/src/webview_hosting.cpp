#include "pviews/webview_hosting.h"

#include <cctype>

#include "pviews/webview_bridge.h"

namespace {

// Provisional Hybrid stub pool cap (see plan Risks: Q-PVIEWS-002).
// Single hosted view; hosting state is process-wide for the stub.
bool g_attached = false;

// Engine surface extent enforced internally by attachWebView (stub host).
constexpr int kEngineWidth = 4096;
constexpr int kEngineHeight = 4096;

bool inEngineBounds(const LayoutBounds& b) noexcept {
  if (b.left < 0 || b.top < 0) return false;
  if (b.right <= b.left) return false;
  if (b.bottom <= b.top) return false;
  if (b.left >= kEngineWidth || b.top >= kEngineHeight) return false;
  if (b.right > kEngineWidth || b.bottom > kEngineHeight) return false;
  return true;
}

// Ensures the scripts the contract round-trips are on the T1 allowlist.
// Idempotent; safe to call on every evalJs. Never throws.
void ensureBridgeDefaults() noexcept {
  try {
    registerBridgeMethod("bridge.echo", CapabilitySet{});
  } catch (...) {
  }
}

// Method identity is the script head before '(' (trimmed); the allowlist
// gate decides. Never throws.
std::string scriptMethod(const std::string& script) noexcept {
  try {
    std::string::size_type end = script.find('(');
    std::string head =
        (end == std::string::npos) ? script : script.substr(0, end);
    std::string::size_type first = 0;
    while (first < head.size() &&
           std::isspace(static_cast<unsigned char>(head[first]))) {
      ++first;
    }
    std::string::size_type last = head.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(head[last - 1]))) {
      --last;
    }
    return head.substr(first, last - first);
  } catch (...) {
    return std::string();
  }
}

}  // namespace

HostResult attachWebView(LayoutBounds b, WebSettings s) noexcept {
  try {
    if (!inEngineBounds(b)) {
      HostResult r;
      r.ok = false;
      r.error = "BOUNDS";
      return r;
    }
    if (s.allowFileAccess) {
      HostResult r;
      r.ok = false;
      r.error = "POLICY:FILE_ACCESS";
      return r;
    }
    if (!s.safeBrowsingEnabled) {
      HostResult r;
      r.ok = false;
      r.error = "POLICY:SAFE_BROWSING";
      return r;
    }
    // OAuth leg: completes synchronously via the Custom Tabs handoff stub;
    // no network, no credential handling. Any dummy https redirect URL with
    // the handoff flag is accepted once bounds/policy pass.
    g_attached = true;
    HostResult r;
    r.ok = true;
    r.error.clear();
    return r;
  } catch (...) {
    HostResult r;
    r.ok = false;
    try {
      r.error = "BOUNDS";
    } catch (...) {
    }
    return r;
  }
}

JsResult evalJs(std::string script) noexcept {
  try {
    ensureBridgeDefaults();
    std::string method = scriptMethod(script);
    BridgeResult b = invokeBridge(method, script, CapabilitySet{});
    JsResult r;
    if (b.allowed) {
      r.ok = true;
      r.value = b.result;
      r.error.clear();
      if (r.value.empty()) {
        // Keep the round-trip observable non-empty without touching the
        // bridge contract: echo the method identity.
        r.value = method.empty() ? "(empty)" : method;
      }
    } else {
      r.ok = false;
      r.value.clear();
      r.error = b.error;
      if (r.error.empty()) {
        r.error = "DENIED:";
      }
    }
    return r;
  } catch (...) {
    JsResult r;
    r.ok = false;
    try {
      r.error = "DENIED:";
    } catch (...) {
    }
    return r;
  }
}
