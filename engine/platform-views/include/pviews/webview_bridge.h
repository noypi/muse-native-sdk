#pragma once

#include <set>
#include <string>

using CapabilitySet = std::set<std::string>;

struct BridgeResult {
  bool allowed = false;
  std::string result;
  std::string error;
};

// Registers `method` on the explicit allowlist with its required
// SEC-SANDBOX capabilities. Only registered methods can be invoked.
void registerBridgeMethod(const std::string& method,
                          const CapabilitySet& required);

// Dispatches a JS bridge call through the allowlist gate: unregistered
// method or missing capability returns {false, "", "DENIED:<method>"};
// deny-by-default. Logs the denial with method identity only
// (never the payload). Never throws.
BridgeResult invokeBridge(std::string method, std::string payload,
                          CapabilitySet caps) noexcept;
