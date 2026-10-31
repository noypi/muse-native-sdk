#include "pviews/webview_bridge.h"

#include <cstdio>
#include <map>
#include <mutex>

namespace {

// Explicit allowlist: method name -> required capabilities.
std::map<std::string, CapabilitySet>& registry() {
  static std::map<std::string, CapabilitySet> methods;
  return methods;
}

std::mutex& registryMutex() {
  static std::mutex mutex;
  return mutex;
}

// Logs denial with method identity only; never includes the payload.
void logDenial(const std::string& method) noexcept {
  try {
    std::fprintf(stderr, "webview_bridge: denied %s\n", method.c_str());
  } catch (...) {
  }
}

BridgeResult denied(const std::string& method) noexcept {
  BridgeResult r;
  try {
    r.allowed = false;
    r.error = "DENIED:" + method;
  } catch (...) {
    r.allowed = false;
    r.result.clear();
    r.error = "DENIED:";
  }
  return r;
}

}  // namespace

void registerBridgeMethod(const std::string& method,
                          const CapabilitySet& required) {
  std::lock_guard<std::mutex> lock(registryMutex());
  registry()[method] = required;
}

BridgeResult invokeBridge(std::string method, std::string payload,
                          CapabilitySet caps) noexcept {
  try {
    CapabilitySet required;
    {
      std::lock_guard<std::mutex> lock(registryMutex());
      const auto it = registry().find(method);
      if (it == registry().end()) {
        logDenial(method);
        return denied(method);
      }
      required = it->second;
    }
    for (const auto& cap : required) {
      if (caps.find(cap) == caps.end()) {
        logDenial(method);
        return denied(method);
      }
    }
    BridgeResult r;
    r.allowed = true;
    r.result = payload;
    return r;
  } catch (...) {
    try {
      logDenial(method);
    } catch (...) {
    }
    return denied(method);
  }
}
